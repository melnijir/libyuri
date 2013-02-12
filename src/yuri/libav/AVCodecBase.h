#ifndef AVCODECBASE_H_
#define AVCODECBASE_H_

#include "yuri/io/BasicIOThread.h"
extern "C" {
	#include "libavcodec/avcodec.h"
	//include "avutil.h"
}
namespace yuri
{
namespace video
{
using namespace yuri::io;
using namespace yuri::log;
using namespace std;
class AVCodecBase: public BasicIOThread
{
public:
	AVCodecBase(Log &_log, pThreadBase parent, string id="libAV", yuri::sint_t inp = 1, yuri::sint_t outp = 1) IO_THREAD_CONSTRUCTOR;
	virtual ~AVCodecBase();
protected:
	bool init_codec(AVMediaType codec_type, int width, int height, int bps, int fps, int fps_base);
	AVFrame * alloc_picture(PixelFormat fmt,int w, int h);
	void free_picture(AVFrame *pix);
	bool find_decoder();
	bool find_encoder();	
	void do_pack_frame_data(PixelFormat fmt, int width, int height, yuri::size_t *size, char **memory, uint8_t **data, int *linesize) __attribute__ ((__deprecated__));
	void do_map_frame_data(PixelFormat fmt, int width, int height, uint8_t **planes, uint8_t *memory, int *linesizes) __attribute__ ((__deprecated__));
	static void initialize_avcodec();
	static shared_ptr<AVFrame> convert_to_avframe(shared_ptr<BasicFrame> frame);
	static shared_ptr<AVPicture> convert_to_avpicture(shared_ptr<BasicFrame> frame);
	template<typename T> static void set_av_frame_or_picture(shared_ptr<BasicFrame> frame, shared_ptr<T> av);
	//static shared_ptr<AVPicture> allocate_avpicture(long format);
	static CodecID get_codec_from_string(string codec);
	static PixelFormat av_pixelformat_from_yuri(yuri::format_t format) throw (Exception);
	static yuri::format_t yuri_pixelformat_from_av(PixelFormat format) throw (Exception);
	static yuri::format_t yuri_format_from_avcodec(CodecID codec) throw (Exception);
	static CodecID avcodec_from_yuri_format(yuri::format_t codec) throw (Exception);
	static void av_frame_deleter(AVFrame*);
	template<typename T> static void av_deleter(T *ptr) { if (ptr) av_free(ptr); }
	static yuri::size_t calculate_time(yuri::size_t timestamp, AVRational &base);
protected: 
	static boost::mutex avcodec_lock;
	static bool avcodec_initialized;
	static map<yuri::format_t, CodecID> yuri_codec_map;
	static map<yuri::format_t, PixelFormat> yuri_pixel_map;
	// Used to convert between 'identical' format. TODO: needs better solution
	static map<PixelFormat, PixelFormat> av_identity;

	AVCodecContext* cc;
	AVCodec *c;
	CodecID codec_id;
	yuri::format_t current_format;
	int width, height, bps, fps, fps_base;
	AVMediaType codec_type;
	bool opened;


};

template<typename T> void AVCodecBase::set_av_frame_or_picture(shared_ptr<BasicFrame> frame,shared_ptr<T> av)
{
	FormatInfo_t fmt = BasicPipe::get_format_info(frame->get_format());
	//log[info] << "Format " << fmt->name << " with " << fmt->planes << " planes" <<endl;
	assert(fmt->planes && fmt->planes == frame->get_planes_count());
	//yuri::size_t width = frame->get_width();
	yuri::size_t height = frame->get_height(), no_planes, bpplane;
	if (frame->get_planes_count() !=  fmt->planes) {
		// This should never happen. Something has gone wrong.
		//log[warning] << "fucked up plane numbers" << endl;
		cout << "fucked up plane numbers (expected: " <<fmt->planes << ", got: " << frame->get_planes_count() << ")"  << endl;
	}
	no_planes = min(frame->get_planes_count(), fmt->planes);
	bpplane = fmt->bpp;
	for (yuri::size_t i = 0; i < 4; ++i) {
		if (no_planes>1) bpplane= fmt->component_depths[i];
		if (i >= no_planes) {
			av->data[i]=0;
			av->linesize[i]=0;
		} else {
			av->data[i]=(uint8_t*)((*frame)[i].data.get());
			//av->linesize[i]=fmt->bpp*width/fmt->plane_x_subs[i]/8;
			if (height) {
				//av->linesize[i]=(*frame)[i].get_size()/(height*fmt->plane_y_subs[i]);

				av->linesize[i]=(bpplane*frame->get_width()/fmt->plane_x_subs[i])>>3;
			} else {
				av->linesize[i]=0;
			}
			//cout << av->linesize[i] << endl;
		}
		//log[debug] << "set linesize[" << i<<"] to " << av->linesize[i] << endl;
	}
}


}
}
#endif /*AVCODECBASE_H_*/
