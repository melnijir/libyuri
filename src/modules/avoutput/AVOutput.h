/*!
 * @file 		RTMP.h
 * @author 		Jiri Melnikov
 * @date 		13.12.2021
 * @date		13.12.2021
 */

#ifndef RTMP_H_
#define RTMP_H_

#include "yuri/core/thread/IOThread.h"
#include "yuri/core/frame/RawVideoFrame.h"
#include "yuri/core/frame/RawAudioFrame.h"
#include "yuri/core/utils/managed_resource.h"
#include <chrono>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace yuri {
namespace avoutput {

struct StreamDescription {
    AVStream       *stream;
    AVCodecContext *enc;
    AVFrame        *frame;
    AVFrame        *tmp_frame;
    AVPacket       *tmp_pkt;
    int64_t        next_pts;
    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
    size_t         bitrate;
    size_t         width;          // Video only
    size_t         height;         // Video only
    int            fps;            // Video only
    AVPixelFormat  video_format;   // Video only
    int            sample_rate;    // Audio only
    size_t         channels;       // Audio only
    AVChannelLayout channel_layout; // Audio only
    AVSampleFormat audio_format;   // Audio only
};

class AVOutput: public yuri::core::IOThread {
    using base_type = yuri::core::IOThread;

public:
    IOTHREAD_GENERATOR_DECLARATION
    static core::Parameters configure();
    AVOutput(const log::Log& _log, core::pwThreadBase parent, const core::Parameters& parameters);
    virtual ~AVOutput() noexcept;
    virtual bool set_param(const core::Parameter& param) override;

private:
    virtual bool step() override;
	void initialize();
	void open_output();
	void deinitialize();

    bool                av_initialized_;

	std::string         url_;
	double              fps_;
	int                 audio_bitrate_;
    int                 video_bitrate_;
    format_t            video_codec_;
    pix_fmt_t           video_format_;
    bool                audio_;

    AVFormatContext*    fmt_ctx_ = nullptr;
    StreamDescription   video_st_;
    StreamDescription   audio_st_;

    std::shared_ptr<yuri::core::RawAudioFrame>  yuri_audio_frame_;
    std::shared_ptr<yuri::core::RawVideoFrame>  yuri_video_frame_;

    /// Interleaved samples waiting for a full encoder frame
    std::vector<uint8_t> audio_fifo_;
    size_t audio_sample_bytes_ = 0;
    /// First video frame of a start that still waits for audio
    bool video_waiting_ = false;
    std::chrono::steady_clock::time_point video_waiting_since_{};
    std::chrono::steady_clock::time_point next_attempt_{};

    size_t   last_video_width;
    size_t   last_video_height;
    format_t last_video_format;
};

} /* namespace rtmp */
} /* namespace yuri */
#endif /* RTMP_H_ */
