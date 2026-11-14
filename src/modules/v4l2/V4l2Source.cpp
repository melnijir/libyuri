/*!
 * @file 		V4l2Source.cpp
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date 		17.5.2009
 * @date		25.1.2015
 * @copyright	Institute of Intermedia, CTU in Prague, 2009 - 2015
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "V4l2Source.h"
#include <algorithm>
//#include <boost/algorithm/string.hpp>
#include "yuri/core/Module.h"
#include <string>
//#include <boost/assign.hpp>

#include <cstring>
#include "yuri/core/frame/RawVideoFrame.h"
#include "yuri/core/frame/raw_frame_params.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/frame/compressed_frame_types.h"
#include "yuri/core/frame/compressed_frame_params.h"
#include "yuri/core/frame/CompressedVideoFrame.h"

#include "yuri/core/thread/InputRegister.h"
#include "yuri/core/utils/make_list.h"
#include "yuri/core/utils/irange.h"


#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "v4l2_device.h"

namespace yuri {

namespace v4l2 {



IOTHREAD_GENERATOR(V4l2Source)
MODULE_REGISTRATION_BEGIN("v4l2source")
	REGISTER_IOTHREAD("v4l2source",V4l2Source)
	REGISTER_INPUT_THREAD("v4l2source", V4l2Source::enumerate)
MODULE_REGISTRATION_END()

#include "v4l2_constants.cpp"

namespace {
capture_method_t parse_method(const std::string& method_s)
{
	if (iequals(method_s,"user")) return capture_method_t::user;
	else if (iequals(method_s,"mmap")) return capture_method_t::mmap;
	else if (iequals(method_s,"read")) return capture_method_t::read;
	else return capture_method_t::none;
}
}

core::Parameters V4l2Source::configure()
{
	core::Parameters p = IOThread::configure();
	p["resolution"]["Resolution of the image. Note that actual resolution may differ"]=resolution_t{640,480};
	p["path"]["Path to the camera device. usually /dev/video0 or similar."]="/dev/video0";
	p["method"]["Method used to get images from camera. Possible values are: none, mmap, user, read. For experts only"]="none";
	p["format"]["Format to capture in."]=0;
	p["input"]["Input number to tune"]=0;
	p["illumination"]["Enable illumination (if present)"]=true;
	p["combine"]["Combine frames (if camera sends them in chunks)."]=false;
	p["fps"]["Number of frames per second requested. The closest LOWER supported value will be selected."]=fraction_t{30,1};
	p["repeat_headers"]["Repeat headers for compressed formats (H264)"]=true;
	return p;
}

std::vector<core::InputDeviceInfo> V4l2Source::enumerate()
{
	std::vector<core::InputDeviceInfo>  devices;
	std::vector<std::string> main_param_order = {"path", "input", "format", "resolution", "fps"};
	for(const auto&x:enum_v4l2_devices()) {
		try {
			v4l2_device f(x);
			core::InputDeviceInfo device;
			device.main_param_order = main_param_order;
			auto info = f.get_info();
			// Raspberry Pi image processors and codecs present themselves as capture devices,
			// but they are no cameras
			static const std::vector<std::string> not_cameras = {"bcm2835-isp", "bcm2835-codec", "pispbe", "rpi-hevc-dec"};
			if (std::find(not_cameras.begin(), not_cameras.end(), info.driver) != not_cameras.end()) continue;
			device.device_name = info.name;

			auto inputs = f.enum_inputs();
			if (inputs.empty()) {
				// Some devices doesn't support input enumeration...
				inputs.push_back({{}, 0});
			}
			for (auto i: irange(0, inputs.size())) {
				if (!f.set_input(i) && i) {
					// Ignore invalid inputs, except for input 0...
					continue;
				}

				for (auto fmt: f.enum_formats()) {
					auto yfmt = v4l2_format_to_yuri(fmt);
					if (!yfmt) continue;

					core::InputDeviceConfig cfg_base;
					cfg_base.params["path"]=x;
					cfg_base.params["input"]=i;
					cfg_base.params["format"]=get_short_yuri_fmt_name(yfmt);
					auto res = f.enum_resolutions(fmt);
					if (res.empty()) {
						// No resolutions supported, let's report what we have
						device.configurations.push_back(std::move(cfg_base));
						continue;
					}
					for (auto r: res) {
						core::InputDeviceConfig cfg_res = cfg_base;
						cfg_res.params["resolution"]=r;
						auto fps_list = f.enum_fps(fmt, r);
						if (fps_list.empty()) {
							device.configurations.push_back(std::move(cfg_res));
						} else {
							for (const auto& fps: fps_list) {
								core::InputDeviceConfig cfg_fps = cfg_res;
								cfg_fps.params["fps"]=fps;
								device.configurations.push_back(std::move(cfg_fps));
							}
						}
					}
				}
			}
			devices.push_back(std::move(device));
		}
		catch (std::exception& e) {
		}
	}
	return devices;
}


V4l2Source::V4l2Source(log::Log &log_,core::pwThreadBase parent, const core::Parameters &parameters)
:core::IOThread(log_,parent,0,1,std::string("v4l2")),
 event::BasicEventConsumer(log),
filename_("/dev/video0"), method_(capture_method_t::none),input_(0),format_(0),
resolution_({640,480}),fps_{30,1},imagesize_(0),allow_empty_(false),
buffer_free_(0),illuminator_(true)
{
	IOTHREAD_INIT(parameters)

	try {
		device_ = open_device();

		// Enable illuminator, if requested by user
//		enable_iluminator();

		enum_controls();


	}
	catch (exception::Exception &e)
	{
		if (!allow_empty_) {
			log[log::fatal] << "Faile to initialize: " << e.what();
			throw exception::InitializationFailed(e.what());
		} else {
			log[log::info] << "Failed to open device, but allow empty was specified, so continuing anyway.";
		}
	}
}

V4l2Source::~V4l2Source() noexcept{

}

namespace {
std::string parse_v4l2_fmt(uint32_t f) {
	std::string out(4,' ');
	out[0]=f&0xFF;
	out[1]=(f>>8)&0xFF;
	out[2]=(f>>16)&0xFF;
	out[3]=(f>>24)&0xFF;
	return out;
}
}

std::unique_ptr<v4l2_device> V4l2Source::open_device()
{
	auto fd_ = make_unique<v4l2_device>(filename_);
	if (!fd_->set_input(input_) && input_ != 0) {
		// Failure to set format is fatal, unless it's for input_ == 0
		throw std::runtime_error("Failed to set input");
	}

	auto fmts = fd_->enum_formats();
	format_t autoselect = format_;
	log[log::debug] << "Supported formats: ";
	for (auto f: fmts) {
		auto yuri_fmt = v4l2_format_to_yuri(f);
		if (yuri_fmt) {
			if (!autoselect) {
				autoselect=yuri_fmt;
			}
			log[log::debug] << "\t" << get_long_yuri_fmt_name(yuri_fmt) << " (" << get_short_yuri_fmt_name(yuri_fmt) << ")";
		} else {
			log[log::debug] << "\tUnknown format: " << parse_v4l2_fmt(f);
		}
	}

	if (!format_ && autoselect) {
		log[log::info] << "Auto selecting format: "<< get_long_yuri_fmt_name(autoselect);
		format_ = autoselect;
	}

	auto pixfmt = yuri_format_to_v4l2(format_);
	auto res = fd_->enum_resolutions(pixfmt);
	log[log::debug] << "Supported resolutions for " <<   get_long_yuri_fmt_name(format_);
	for(auto r: res) {
		auto l = log[log::debug];
		l << "\t" << r << ", fps: ";
		auto fps_list = fd_->enum_fps(pixfmt, r);
		core::utils::print_list(l, fps_list);
	}


	auto info = fd_->set_format(pixfmt, resolution_);
	imagesize_ = info.imagesize;
	resolution_ = info.resolution;
	log[log::info] << "Initialized for resolution " << resolution_ << ", with image size: " << imagesize_ << "B";

	auto fps = fd_->set_fps(fps_);
	if (!fps.valid()) {
		log[log::warning] << "Failed to set fps";
	} else {
		fps_ = fps;
		log[log::info] << "Driver reports framerate: " << fps_;
	}

	if (!fd_->initialize_capture(imagesize_, method_, log))
	{
		log[log::error] << "Failed to initialize capture";
		throw std::runtime_error("Failed to initialize capture");

	}
	controls::set_control(*fd_, "illuminator", illuminator_, log);
	return fd_;
}

namespace {
void set_control_impl(const std::string& event_name, const event::pBasicEvent& event, std::vector<controls::control_info> controls, log::Log& log, v4l2_device& dev)
{
	auto it = std::find_if(controls.cbegin(), controls.cend(), [&event_name](const controls::control_info&info){return iequals(info.name, event_name);});
	if (it != controls.cend()) {
		controls::set_control(dev, it->id, event, log);
	} else if (!controls::set_control(dev, event_name, event, log)) {
//		log[log::warning] << "set control failed " << event_name;
	}

}
void set_controls_from_map(std::map<std::string, event::pBasicEvent>& ev_map, std::vector<controls::control_info> controls, log::Log& log, v4l2_device& dev)
{
	for (const auto& p: ev_map) {
		set_control_impl(p.first, p.second, controls, log, dev);
	}
}
}

void V4l2Source::run()
{
	while (!device_->start_capture()) {
		sleep(get_latency());
		if (!still_running()) return;
	}
	log[log::info] << "Capture started";
	while (still_running()) {
		process_events();
		if (!control_tmp_.empty()) {
			if (device_) set_controls_from_map(control_tmp_, controls_, log, *device_);
			control_tmp_.clear();
		}
		if (device_->wait_for_data(get_latency())) {
			//device_->read_frame([this](void*,size_t s)->bool{log[log::info]<<"GOt frame with " << s << " bytes"; return true;});
			device_->read_frame([this](uint8_t*p,size_t s){return prepare_frame(p,s);});
		}
		if (!buffer_free_ && output_frame_) {
			push_frame(0, std::move(output_frame_));
		}
	}
	log[log::info] << "Stopping capture";
	device_->stop_capture();
}


bool V4l2Source::set_param(const core::Parameter &param)
{
	if (assign_parameters(param)
			(filename_, "path")
			(resolution_, "resolution")
			(input_, "input")
			(illuminator_, "illumination")
			(combine_frames_, "combine")
			(fps_, "fps")
				.parsed<std::string>(method_, "method", parse_method)
			(repeat_headers_, "repeat_headers")
			)
		return true;

	if (param.get_name() == "format") {
		std::string format = param.get<std::string>();
		format_ = core::raw_format::parse_format(format);
		if (!format_) {
			format_ = core::compressed_frame::parse_format(format);
		}
		if (!format_) {
			log[log::info] << "Input format not specified or not understood, the format will be detected automatically.";
		}
		return true;
	}
	return IOThread::set_param(param);


}
bool V4l2Source::prepare_frame(uint8_t *data, yuri::size_t size)
{
	if (!format_) return false;

	try {
		const raw_format_t& fi = core::raw_format::get_format_info(format_);

		core::pRawVideoFrame rframe = std::dynamic_pointer_cast<core::RawVideoFrame>(output_frame_);
		if (!rframe) {
			rframe = core::RawVideoFrame::create_empty(format_, resolution_, true);
			buffer_free_ = PLANE_SIZE(rframe, 0);
			output_frame_ = rframe;
		}
		const auto frame_size = PLANE_SIZE(rframe, 0);;
		auto frame_position = frame_size - buffer_free_;
		log[log::verbose_debug] << "Frame " << resolution_.width << ", " << resolution_.height << ", size: " << size;
		if (fi.planes.size()==1) {
			if (size > buffer_free_) size = buffer_free_;
			std::copy(data, data + size, PLANE_DATA(rframe, 0).begin());
			buffer_free_ -= size;
		} else {
			yuri::size_t offset = 0;
			for (yuri::size_t i = 0; i < fi.planes.size(); ++i) {
				if (!size) break;
				yuri::size_t cols = resolution_.width / fi.planes[i].sub_x;
				yuri::size_t rows = resolution_.height / fi.planes[i].sub_y;
				yuri::size_t plane_size = (cols*rows*fi.planes[i].bit_depth.first/fi.planes[i].bit_depth.second)>>3;
				//if(size<offset+plane_size) return pBasicFrame();
				if (plane_size > frame_position) {
					plane_size -= frame_position;
					frame_position = 0;
				} else {
					frame_position-=plane_size;
					continue;
				}
				if(size<offset+plane_size) {
					plane_size = size-offset;
				}
				if (plane_size > buffer_free_) {
					plane_size = buffer_free_;
				}
				log[log::info] << "Copying " << plane_size << " bytes, have " << size-offset <<", free buffer: " << buffer_free_;
				std::copy(data+offset, data+offset+plane_size, PLANE_DATA(rframe, i).begin());
				offset+=plane_size;
				buffer_free_-=plane_size;
			}
		}
	}
	catch (std::runtime_error& ) {
		core::pCompressedVideoFrame cframe;
		bool add_headers = false;
		if (format_ == core::compressed_frame::h264) {
			if (headers_.empty()) {
				headers_.insert(headers_.end(), data, data + size);
				cframe = core::CompressedVideoFrame::create_empty(format_, resolution_, data, size);
			} else {
				// Let's try to find IDR
				if ((size > 4 && data[0]==0 && data[1]==0 && data[2]==1 && (data[3]&0x1F) == 5) ||
					(size > 5 && data[0]==0 && data[1]==0 && data[2] == 0 && data[3]==1 && (data[4]&0x1F) == 5))
				{
					add_headers = true;
				}
			}
		}
		if (add_headers) {
			cframe = core::CompressedVideoFrame::create_empty(format_, resolution_, size + headers_.size());
			std::copy(headers_.begin(), headers_.end(), cframe->begin());
			std::copy(data, data+size, cframe->begin() + headers_.size());
		} else {
			cframe = core::CompressedVideoFrame::create_empty(format_, resolution_, data, size);
		}
		buffer_free_ = 0;//frame_size;
		output_frame_ = cframe;


	}

	// If we're no combining frames, we have to discard incomplete ones
	if (buffer_free_ && !combine_frames_) {

		log[log::warning] << "Discarding incomplete frame (missing " << buffer_free_ << " bytes)";
		buffer_free_ = 0;
		output_frame_.reset();
	}
	return true;
}

bool V4l2Source::enum_controls()
{
	if (!device_) return false;
	controls_ = controls::get_control_list(*device_, log);
	log[log::info] << "Supported controls:";
	for (const auto& c: controls_) {
		log[log::info] << "\t'" << c.name << "' (" << c.short_name
				<< "), value: " << c.value
				<< ", range: <" << c.min_value<<", "<<c.max_value<<">";
	}
	return true;
}

bool V4l2Source::do_process_event(const std::string& event_name, const event::pBasicEvent& event)
{
	control_tmp_[event_name]=event;
	return true;
}

}
}

