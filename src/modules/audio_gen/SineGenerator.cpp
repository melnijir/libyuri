/*!
 * @file 		SineGenerator.cpp
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date 		22.03.2015
 * @copyright	Institute of Intermedia, CTU in Prague, 2015
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "SineGenerator.h"
#include "yuri/core/Module.h"
#include "yuri/core/frame/RawAudioFrame.h"
#include "yuri/core/frame/raw_audio_frame_types.h"
#include "yuri/core/utils/irange.h"
#include <cmath>
namespace yuri {
namespace audio_gen {


IOTHREAD_GENERATOR(SineGenerator)

core::Parameters SineGenerator::configure()
{
	core::Parameters p = core::IOThread::configure();
	p.set_description("SineGenerator");
	p["frequency"]["Frequency of generated signal"] = 1000.0;
	p["channels"]["Number of channels"]=1;
	p["sampling_frequency"]["Sampling frequency of generated signal"] = 48000;
	p["samples"]["Number of samples to generate at time"]=4800;

	return p;
}


SineGenerator::SineGenerator(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
core::IOThread(log_,parent,0,1,std::string("sine_generator")),
frequency_(1000.0),t_(0.0),sample_(0),channels_(1),sampling_(48000),samples_(4800)
{
	IOTHREAD_INIT(parameters)
}

SineGenerator::~SineGenerator() noexcept
{
}


void SineGenerator::run()
{
	Timer timer;
	const auto pi = 4.0*std::atan(1.0);
	const auto max_value = std::numeric_limits<int16_t>::max();
	const auto delta = 1_s * samples_ / sampling_;

	while(still_running()) {
		if (timer.get_duration() < t_) {
			sleep(t_ - timer.get_duration());
			continue;
		}
		auto frame = core::RawAudioFrame::create_empty(core::raw_audio_format::signed_16bit,
				channels_, sampling_, samples_);
		auto d = reinterpret_cast<int16_t*>(frame->data());
		auto d_end = d + samples_ * channels_;

		while(d < d_end) {
			auto v = static_cast<int16_t>(max_value * std::sin(2.0 * pi * sample_ * frequency_ / sampling_));
			for (auto i: irange(0, channels_)) {
				(void)i;
				*d++=v;
			}
			++sample_;
		}
		sample_ = sample_ % sampling_;
		t_ += delta;

		push_frame(0, frame);
	}
}
bool SineGenerator::set_param(const core::Parameter& param)
{
	if (assign_parameters(param)
			(frequency_, 	"frequency")
			(channels_,		"channels")
			(sampling_,		"sampling_frequency")
			(samples_,		"samples"))
		return true;
	return core::IOThread::set_param(param);
}

} /* namespace audio_gen */
} /* namespace yuri */
