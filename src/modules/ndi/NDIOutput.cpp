/*!
 * @file        NDIOutput.cpp
 * @author      Jiri Melnikov <jiri@melnikoff.org>
 * @date        20.12.2023
 *              Distributed under modified BSD Licence, details in file doc/LICENSE
 */

#include "NDIOutput.h"

#include "yuri/core/Module.h"
#include "yuri/core/frame/RawVideoFrame.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/frame/raw_frame_params.h"
#include "yuri/core/frame/compressed_frame_types.h"
#include "yuri/core/frame/compressed_frame_params.h"
#include "yuri/core/frame/RawAudioFrame.h"
#include "yuri/core/frame/raw_audio_frame_types.h"
#include "yuri/core/frame/raw_audio_frame_params.h"

#include "yuri/core/utils.h"

#include "rapidxml.hpp"

#include "utils.h"

#include <thread>
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>

#include <iostream>
#include <fstream>

namespace yuri {
namespace ndi {

IOTHREAD_GENERATOR(NDIOutput)

core::Parameters NDIOutput::configure() {
    core::Parameters p = IOThread::configure();
    p["stream"]["Name of the stream to send."]="Dicaffeine";
    p["audio"]["Set to true if audio should be send."]=false;
    p["fps"]["Sets fps indicator sent in the stream (0 = default)"]=0.0;
    p["ptz"]["Support for PTZ control"]=false;
    p["ndi_path"]["Path where to find the NDI libraries, if empty, env variable NDI_PATH is used."]="";
    return p;
}

NDIOutput::NDIOutput(log::Log &log_,core::pwThreadBase parent, const core::Parameters &parameters)
:core::IOThread(log_,parent,1,0,std::string("NDIOutput")),
event::BasicEventProducer(log),event::BasicEventConsumer(log),
stream_("Dicaffeine"),audio_enabled_(false),fps_(0),ptz_(false),ndi_path_("") {
    IOTHREAD_INIT(parameters)
    set_latency(10_us);
    if (audio_enabled_) resize(2,0);
    // Load NDI library
    NDIlib_ = load_ndi_library(ndi_path_);
    // Init NDI
    if (!NDIlib_->initialize())
        throw exception::InitializationFailed("Failed to initialize NDI input.");
}

NDIOutput::~NDIOutput() {
}

void NDIOutput::run() {
    NDIlib_send_create_t NDI_send_create_desc;
    NDI_send_create_desc.p_ndi_name = stream_.c_str();
    if (audio_enabled_) {
        NDI_send_create_desc.clock_audio = true;
    }
    pNDI_send_ = NDIlib_->send_create(&NDI_send_create_desc);
    if (!pNDI_send_)
        throw exception::InitializationFailed("Failed to initialize NDI sender.");

    if (ptz_) {
        NDIlib_metadata_frame_t NDI_capabilities;
        NDI_capabilities.p_data = strdup("<ndi_capabilities ntk_ptz=\"true\""
                                         "                  web_control=\"https://dicaffeine.com\""
                                         "                  ntk_exposure_v2=\"false\"/>");
        NDIlib_->send_add_connection_metadata(pNDI_send_, &NDI_capabilities);
    }

    std::thread audio_thread(&NDIOutput::audio_sender, this);
    std::thread events_thread(&NDIOutput::events_sender, this);
    IOThread::run();
    stop_stream();
    audio_thread.join();
    events_thread.join();
}

void NDIOutput::audio_sender() {
    while (still_running() && audio_enabled_) {
        aframe_to_send_ = std::dynamic_pointer_cast<core::RawAudioFrame>(pop_frame(1));
        if (aframe_to_send_) {
            NDIlib_audio_frame_interleaved_16s_t NDI_audio_frame;
            NDI_audio_frame.no_channels = aframe_to_send_->get_channel_count();
            NDI_audio_frame.no_samples = aframe_to_send_->get_sample_count();
            NDI_audio_frame.sample_rate = aframe_to_send_->get_sampling_frequency();
            NDI_audio_frame.p_data = (short*)aframe_to_send_->data();
            NDIlib_->util_send_send_audio_interleaved_16s(pNDI_send_, &NDI_audio_frame);
            aframe_to_send_ = nullptr;
        } else {
            ThreadBase::sleep(10_us);
        }
    }
}

void NDIOutput::events_sender() {
    NDIlib_metadata_frame_t metadata_cmd;
    while (still_running()) {
        while (NDIlib_->send_capture(pNDI_send_, &metadata_cmd, ndi_wait_time_ms_) == NDIlib_frame_type_metadata) {
            try {
                // Get the parser
                std::string xml(metadata_cmd.p_data);
                rapidxml::xml_document<char> parser;
                parser.parse<0>((char*)xml.data());
                // Get the tag
                rapidxml::xml_node<char>* p_node = parser.first_node();
                // Check its a node
                if ((!p_node) || (p_node->type() != rapidxml::node_element)) continue;
                // Get the values
                if (!::strcasecmp(p_node->name(), "ntk_ptz_pan_tilt_speed")) {
                    const rapidxml::xml_attribute<char>* p_pan_speed = p_node->first_attribute("pan_speed");
                    const rapidxml::xml_attribute<char>* p_tilt_speed = p_node->first_attribute("tilt_speed");
                    const float pan_speed = p_pan_speed ? (float)::atof(p_pan_speed->value()) : 0.0f;
                    const float tilt_speed = p_tilt_speed ? (float)::atof(p_tilt_speed->value()) : 0.0f;
                    emit_event("pan_speed", pan_speed);
                    emit_event("tilt_speed", tilt_speed);
                }
            } catch (...) {
                // Not a valid message
            }
        }
    }
}

bool NDIOutput::step() {
    auto frame_to_send = pop_frame(0);
    // Skip if there is no frame to send
    if (!frame_to_send)
        return true;
    // Skip if there are no connections
    if (!NDIlib_->send_get_no_connections(pNDI_send_, ndi_wait_time_ms_)) {
        return true;
    }
    // Skip if the frame is not raw
    vframe_to_send_ = std::dynamic_pointer_cast<core::RawVideoFrame>(frame_to_send);
    if (!vframe_to_send_) {
        log[log::warning] << "Only raw frames are currently supported.";
        return true;
    }
    // Skip if NDI does not support the format
    NDIlib_FourCC_type_e ndi_fmt;
    try {
        ndi_fmt = yuri_format_to_ndi(vframe_to_send_->get_format());
    } catch (const exception::Exception& e) {
        log[log::warning] << "Raw frame with format \"" << core::raw_format::get_format_name(vframe_to_send_->get_format()) << "\" not supported by NDI library.";
        return true;
    }
    NDIlib_video_frame_v2_t NDI_video_frame;
    NDI_video_frame.xres = vframe_to_send_->get_width();
    NDI_video_frame.yres = vframe_to_send_->get_height();
    NDI_video_frame.FourCC = ndi_fmt;
    NDI_video_frame.line_stride_in_bytes = 0; // autodetect
    if (fps_ > 0 && fps_ == std::ceil(fps_)) {
        NDI_video_frame.frame_rate_N = 1000*fps_;
        NDI_video_frame.frame_rate_D = 1000;
    } else if (fps_ > 0) {
        int fps = std::ceil(fps_);
        NDI_video_frame.frame_rate_N = 1000*fps;
        NDI_video_frame.frame_rate_D = 1001;
    }
    NDIlib_tally_t NDI_tally;
    NDIlib_->send_get_tally(pNDI_send_, &NDI_tally, 0);
    NDI_video_frame.p_data = PLANE_RAW_DATA(vframe_to_send_,0);
    NDIlib_->send_send_video_v2(pNDI_send_, &NDI_video_frame);
    return true;
}

void NDIOutput::stop_stream() {
    NDIlib_->send_destroy(pNDI_send_);
    NDIlib_->destroy();
}

bool NDIOutput::do_process_event(const std::string& event_name, const event::pBasicEvent& event) {
    if (iequals(event_name, "quit")) {
        request_end(core::yuri_exit_interrupted);
        return true;
    }
    log[log::info] << "Got unknown event \"" << event_name << "\", timestamp: " << event->get_timestamp();
    return false;
}

bool NDIOutput::set_param(const core::Parameter &param) {
    if (assign_parameters(param)
            (stream_,        "stream")
            (audio_enabled_, "audio")
            (fps_,           "fps")
            (ptz_,           "ptz")
            (ndi_path_,      "ndi_path"))
        return true;
    return IOThread::set_param(param);
}

}
}
