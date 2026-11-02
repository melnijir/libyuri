/*!
 * @file 		Combine.cpp
 * @author 		Zdenek Travnicek
 * @date 		11.2.2013
 * @date		16.2.2013
 * @copyright	Institute of Intermedia, CTU in Prague, 2013
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "Combine.h"
#include "yuri/core/Module.h"
#include "yuri/core/frame/RawVideoFrame.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/frame/raw_frame_params.h"
#include <array>
#include <map>
namespace yuri {
namespace ndi_combine {

IOTHREAD_GENERATOR(Combine)

MODULE_REGISTRATION_BEGIN("ndi_combine")
	REGISTER_IOTHREAD("ndi_combine",Combine)
MODULE_REGISTRATION_END()

using namespace yuri::log;

namespace {

/*
 * Pixel access for the packed formats the grid draws into: black cells, dimmed cells and the
 * "NO SIGNAL" text need to know where luma is. Other packed formats are only copied.
 */
struct Layout {
	enum class kind_t { yuv422, rgb, other } kind = kind_t::other;
	/// Bytes per pixel (rgb), offset of the first luma byte (yuv422), alpha byte (rgb, -1 = none)
	size_t bytes = 0;
	size_t luma = 0;
	int alpha = -1;
};

Layout layout_of(format_t format)
{
	using namespace core::raw_format;
	Layout l;
	switch (format) {
		case yuyv422: case yvyu422: l.kind = Layout::kind_t::yuv422; l.luma = 0; break;
		case uyvy422: case vyuy422: l.kind = Layout::kind_t::yuv422; l.luma = 1; break;
		case rgb24: case bgr24: l.kind = Layout::kind_t::rgb; l.bytes = 3; break;
		case rgba32: case bgra32: l.kind = Layout::kind_t::rgb; l.bytes = 4; l.alpha = 3; break;
		case argb32: case abgr32: l.kind = Layout::kind_t::rgb; l.bytes = 4; l.alpha = 0; break;
		default: break;
	}
	return l;
}

/// A rectangle of the output frame
struct Area {
	uint8_t* data;
	size_t line;	// bytes per line of the frame
	size_t width;	// pixels
	size_t height;
	size_t width_bytes;
};

void fill_black(const Layout& l, const Area& a)
{
	for (size_t y = 0; y < a.height; ++y) {
		uint8_t* row = a.data + y * a.line;
		switch (l.kind) {
			case Layout::kind_t::yuv422:
				for (size_t b = 0; b < a.width_bytes; ++b) row[b] = (b % 2 == l.luma) ? 16 : 128;
				break;
			case Layout::kind_t::rgb:
				for (size_t b = 0; b < a.width_bytes; ++b) row[b] = (l.alpha >= 0 && b % l.bytes == static_cast<size_t>(l.alpha)) ? 255 : 0;
				break;
			default:
				std::fill(row, row + a.width_bytes, 0);
		}
	}
}

/// Half brightness (and saturation), for an input that stopped sending
void dim(const Layout& l, const Area& a)
{
	for (size_t y = 0; y < a.height; ++y) {
		uint8_t* row = a.data + y * a.line;
		switch (l.kind) {
			case Layout::kind_t::yuv422:
				for (size_t b = 0; b < a.width_bytes; ++b)
					row[b] = (b % 2 == l.luma) ? 16 + (std::max<int>(row[b], 16) - 16) / 2 : 128 + (row[b] - 128) / 2;
				break;
			case Layout::kind_t::rgb:
				for (size_t b = 0; b < a.width_bytes; ++b)
					if (l.alpha < 0 || b % l.bytes != static_cast<size_t>(l.alpha)) row[b] /= 2;
				break;
			default:
				break;
		}
	}
}

/// Grey pixel of the given luma (the area is black, so yuv422 chroma is neutral already)
void plot(const Layout& l, const Area& a, size_t x, size_t y, uint8_t value)
{
	if (x >= a.width || y >= a.height) return;
	uint8_t* row = a.data + y * a.line;
	if (l.kind == Layout::kind_t::yuv422) {
		row[x * 2 + l.luma] = value;
	} else if (l.kind == Layout::kind_t::rgb) {
		for (size_t c = 0; c < l.bytes; ++c)
			if (static_cast<int>(c) != l.alpha) row[x * l.bytes + c] = value;
	}
}

// 5x7 glyphs of "NO SIGNAL", one row per byte, bit 4 = leftmost column
const std::map<char, std::array<uint8_t, 7>> glyphs = {
	{'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
	{'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
	{'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
	{'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
	{'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e}},
	{'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
	{'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
};

/// A black cell with "NO SIGNAL" in the middle, about half the cell wide
void no_signal(const Layout& l, const Area& a)
{
	fill_black(l, a);
	if (l.kind == Layout::kind_t::other) return;
	const std::string text = "NO SIGNAL";
	const size_t units_w = text.size() * 6 - 1;
	const size_t scale = std::max<size_t>(1, a.width / units_w / 2);
	const size_t x0 = (a.width - std::min(a.width, units_w * scale)) / 2;
	const size_t y0 = (a.height - std::min(a.height, 7 * scale)) / 2;
	for (size_t i = 0; i < text.size(); ++i) {
		auto g = glyphs.find(text[i]);
		if (g == glyphs.end()) continue;
		for (size_t gy = 0; gy < 7; ++gy)
			for (size_t gx = 0; gx < 5; ++gx)
				if (g->second[gy] & (0x10 >> gx))
					for (size_t sy = 0; sy < scale; ++sy)
						for (size_t sx = 0; sx < scale; ++sx)
							plot(l, a, x0 + (i * 6 + gx) * scale + sx, y0 + gy * scale + sy, 140);
	}
}

}

core::Parameters Combine::configure()
{
	core::Parameters p = base_type::configure();
	p.set_description("NDI Combine: inputs in an x×y grid. A missing input shows a black cell with NO SIGNAL, "
			"an input that stopped sending is dimmed.");
	p["x"]["Width of the grid"]=2;
	p["y"]["Height of the grid"]=2;
	p["fps"]["Maximal framerate"]=30;
	p["cell"]["Size of one cell (0x0 = size of the first input that sends a frame)"]=resolution_t{0, 0};
	p["stale"]["Dim a cell whose input sent no new frame for this many seconds (0 = never)"]=2.0;
	p["timeout"]["Show NO SIGNAL when an input sent no new frame for this many seconds (0 = never)"]=10.0;
	return p;
}


Combine::Combine(log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
base_type(log_,parent,1,1,std::string("ndi_combine")),x_(2),y_(2),fps_(30),cell_{0, 0},stale_(2.0),timeout_(10.0),
format_(0)
{
	IOTHREAD_INIT(parameters)
	if (x_<1 || y_<1) throw exception::InitializationFailed("Wrong size of the grid");
	if (fps_ <= 0) throw exception::InitializationFailed("fps has to be positive");
	resize(x_ * y_,1);
	last_.resize(x_ * y_);
	changed_.resize(x_ * y_);
	format_warned_.resize(x_ * y_, false);
	next_time_ = timestamp_t{};
}

Combine::~Combine() noexcept
{
}

bool Combine::step()
{
	// not MultiIOFilter's step: the grid is composed also while some inputs send nothing, and
	// an input is alive when frames arrive (a source may send the same frame object again)
	const timestamp_t now;
	for (size_t i = 0; i < last_.size(); ++i) {
		while (auto f = pop_frame(i)) {
			last_[i] = f;
			changed_[i] = now;
		}
	}
	for (auto& f: do_single_step({})) {
		if (f) push_frame(0, f);
	}
	return true;
}

/// Composes the grid from the last frame of every input (the argument is not used)
std::vector<core::pFrame> Combine::do_single_step(std::vector<core::pFrame>)
{
	const auto curtime = timestamp_t{};
	if (next_time_ > curtime) {
		sleep(std::min(get_latency(), next_time_ - curtime));
		return {};
	}
	const auto period = 1_s / fps_;
	// after a pause (no inputs at all), start over instead of catching up with a burst of frames
	next_time_ = (curtime - next_time_ > period) ? curtime + period : next_time_ + period;

	const size_t frames_no = last_.size();
	std::vector<core::pRawVideoFrame> frames(frames_no);
	for (size_t i = 0; i < frames_no; ++i) {
		if (!last_[i]) continue;
		frames[i] = std::dynamic_pointer_cast<core::RawVideoFrame>(last_[i]);
		if (!frames[i] && !format_warned_[i]) {
			log[log::warning] << "Received non-raw frame on input " << i;
			format_warned_[i] = true;
		}
	}

	// format and cell size: from the first input that sends something, kept from then on
	for (const auto& f: frames) {
		if (!f) continue;
		if (!format_) format_ = f->get_format();
		if (!cell_.width || !cell_.height) cell_ = f->get_resolution();
		break;
	}
	if (!format_ && cell_.width && cell_.height) format_ = core::raw_format::uyvy422;
	if (!format_ || !cell_.width || !cell_.height) return {};	// nothing to show yet

	const auto& fi = core::raw_format::get_format_info(format_);
	if (fi.planes.size() != 1) {
		log[log::warning] << "Planar formats not supported";
		return {};
	}
	const Layout layout = layout_of(format_);
	const size_t bpp = fi.planes[0].bit_depth.first/fi.planes[0].bit_depth.second;
	const size_t width  = cell_.width;
	const size_t height = cell_.height;

	core::pRawVideoFrame output = core::RawVideoFrame::create_empty(format_,{width*x_, height*y_}, true);
	uint8_t* out = PLANE_RAW_DATA(output,0);
	const size_t sub_line_width=bpp*width/8;
	const size_t line_width = sub_line_width*x_;
	size_t idx = 0;
	for (size_t idx_y=0;idx_y<y_;++idx_y) {
		for (size_t idx_x=0;idx_x<x_;++idx_x, ++idx) {
			const Area cell{out + idx_y*height*line_width + idx_x*sub_line_width, line_width, width, height, sub_line_width};
			auto frame = idx < frames_no ? frames[idx] : core::pRawVideoFrame{};
			if (frame && frame->get_format() != format_) {
				if (!format_warned_[idx]) {
					log[log::warning] << "Wrong format for frame in pipe " << idx;
					format_warned_[idx] = true;
				}
				frame.reset();
			}
			const double age = frame ? (curtime - changed_[idx]).value / 1e6 : 0;
			if (!frame || (timeout_ > 0 && age > timeout_)) {
				no_signal(layout, cell);
				continue;
			}
			// a smaller frame leaves the rest of the cell black, a larger one is cropped
			const size_t src_line = bpp*frame->get_width()/8;
			const size_t copy_width = std::min(src_line, sub_line_width);
			const size_t copy_height = std::min(height, frame->get_height());
			if (copy_width < sub_line_width || copy_height < height) fill_black(layout, cell);
			const uint8_t* raw_src = PLANE_RAW_DATA(frame,0);
			for (size_t line=0;line<copy_height;++line) {
				std::copy(raw_src+line*src_line, raw_src+line*src_line+copy_width, cell.data+line*line_width);
			}
			if (stale_ > 0 && age > stale_) dim(layout, cell);
		}
	}
	return {output};
}

bool Combine::set_param(const core::Parameter& param)
{
	if (assign_parameters(param)
			(x_, "x")
			(y_, "y")
			(fps_, "fps")
			(cell_, "cell")
			(stale_, "stale")
			(timeout_, "timeout"))
		return true;
	return base_type::set_param(param);
}

} /* namespace ndi_combine */
} /* namespace yuri */
