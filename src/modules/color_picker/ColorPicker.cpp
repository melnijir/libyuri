/*!
 * @file 		ColorPicker.cpp
 * @author 		<Your name>
 * @date		15.03.2014
 * @copyright	Institute of Intermedia, 2013
 * 				Distributed BSD License
 *
 */

#include "ColorPicker.h"
#include "yuri/core/Module.h"
#include "yuri/core/utils/assign_events.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/utils/color.h"
#include "common.h"
#include <numeric>
#include <array>
namespace yuri {
namespace color_picker {


IOTHREAD_GENERATOR(ColorPicker)

MODULE_REGISTRATION_BEGIN("color_picker")
		REGISTER_IOTHREAD("color_picker",ColorPicker)
MODULE_REGISTRATION_END()

core::Parameters ColorPicker::configure()
{
	core::Parameters p = base_type::configure();
	p.set_description("ColorPicker");
	p["geometry"]["Rectangle to get the color from"]=geometry_t{10,10,0,0};
	p["matrix"]["Divide the rectangle into a matrix (use 1x1 to disable)"]=resolution_t{1,1};
	return p;
}


ColorPicker::ColorPicker(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
base_type(log_,parent,std::string("color_picker")),BasicEventConsumer(log),
event::BasicEventProducer(log),
geometry_(geometry_t{10,10,0,0}),show_color_(true)
{
	IOTHREAD_INIT(parameters)
	using namespace core::raw_format;
	set_supported_formats({rgb24, bgr24, rgb48, bgr48,
							rgba32, bgra32, argb32, abgr32,
							rgba64, bgra64, argb64, abgr64,
							yuyv422, yvyu422, uyvy422, vyuy422,
							yuv444, ayuv4444, yuva4444,

							r8,
							r16,
							g8,
							g16,
							b8,
							b16,
							y8,
							y16,
							u8,
							u16,
							v8,
							v16,
//							depth8,
//							depth16,

	});
}

ColorPicker::~ColorPicker() noexcept
{
}


core::pFrame ColorPicker::do_special_single_step(core::pRawVideoFrame frame)
{
	process_events();

	try {
		core::color_t color;

		const auto w = geometry_.width / matrix_.width;
		const auto h = geometry_.height / matrix_.height;
		for (auto x: irange(matrix_.width)) {
			for (auto y: irange(matrix_.height)) {
				geometry_t g = {
						w,
						h,
						static_cast<position_t>(geometry_.x + x * w),
						static_cast<position_t>(geometry_.y + y * h)
				};
				std::tie(frame, color) = process_rect(std::move(frame), g, show_color_);

				emit_event("color_"+std::to_string(x)+"_"+std::to_string(y), lexical_cast<std::string>(color));
			}
		}


		if (matrix_.width == 1 && matrix_.height == 1) {
			log[log::verbose_debug] << "Found color: " << color;
			emit_event("color", lexical_cast<std::string>(color));
		}
	}
	catch (std::exception&) {
		log[log::warning] << "Failed to get color";

	}

	return frame;
}
bool ColorPicker::set_param(const core::Parameter& param)
{
	if (assign_parameters(param)
			(geometry_, "geometry")
			(matrix_, "matrix"))
		return true;
	return base_type::set_param(param);
}


bool ColorPicker::do_process_event(const std::string& event_name, const event::pBasicEvent& event)
{
	if (assign_events(event_name, event)
			.vector_values("geometry", geometry_.x, geometry_.y, geometry_.width, geometry_.height)
			.vector_values("geometry", geometry_.x, geometry_.y)
			(geometry_, 		"geometry")
			(geometry_.x, 		"x")
			(geometry_.y, 		"y")
			(geometry_.width, 	"width")
			(geometry_.height, 	"height")
			(matrix_,			"matrix"))
		return true;
	return false;
}

} /* namespace color_picker */
} /* namespace yuri */
