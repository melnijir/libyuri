/*!
 * @file 		MidiDevice.h
 * @author 		<Your name>
 * @date 		12.07.2013
 * @copyright	Institute of Intermedia, 2013
 * 				Distributed under GNU Public License 3.0
 *
 */

#ifndef MIDIDEVICE_H_
#define MIDIDEVICE_H_

#include "yuri/core/BasicIOThread.h"
#include "yuri/event/BasicEventProducer.h"
#include <alsa/asoundlib.h>

namespace yuri {
namespace midi_device {

class MidiDevice: public core::BasicIOThread, public event::BasicEventProducer
{
public:
	IO_THREAD_GENERATOR_DECLARATION
	static core::pParameters configure();
	virtual ~MidiDevice();
private:
	MidiDevice(log::Log &log_, core::pwThreadBase parent, core::Parameters &parameters);
	virtual void run();

	virtual bool set_param(const core::Parameter& param);
	bool process_event(const snd_seq_event_t& midievent);
//	snd_rawmidi_t*		input_;
	snd_seq_t*			sequencer_;
	std::string			device_;
	int 				port_;
	std::string			connection_;
};

} /* namespace midi_device */
} /* namespace yuri */
#endif /* MIDIDEVICE_H_ */