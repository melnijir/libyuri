/*!
 * @file 		MutterScreen.h
 * @author 		Jiri Melnikov
 * @date		14.11.2026
 * @copyright	CESNET, z.s.p.o, 2026
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef MUTTERSCREEN_H_
#define MUTTERSCREEN_H_

#include "yuri/core/thread/IOThread.h"
#include "yuri/core/thread/InputThread.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace yuri {
namespace mutter_screen {

struct Session;
struct Stream;

/// Captures a monitor of a GNOME desktop (Wayland or X11) through the screen cast service of
/// Mutter (org.gnome.Mutter.ScreenCast, the one GNOME Remote Desktop uses): no dialog, the
/// picture arrives as a PipeWire stream.
class MutterScreen: public core::IOThread
{
public:
	IOTHREAD_GENERATOR_DECLARATION
	static core::Parameters configure();
	/// Monitors that can be captured (empty without GNOME)
	static std::vector<core::InputDeviceInfo> enumerate();
	MutterScreen(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters);
	virtual ~MutterScreen() noexcept;

	/// Called from the PipeWire thread with a mapped frame
	void on_frame(const uint8_t* data, uint32_t width, uint32_t height, uint32_t stride, bool bgr, bool opaque);
	/// Called from the PipeWire thread when the stream ends or fails
	void on_stream_error(const std::string& message);
private:
	virtual void run() override;
	virtual bool set_param(const core::Parameter &param) override;

	std::string monitor_;
	double fps_;
	bool cursor_;
	coordinates_t position_;
	resolution_t resolution_;

	std::unique_ptr<Session> session_;
	std::unique_ptr<Stream> stream_;
	timestamp_t last_frame_;
	bool first_frame_ = true;
	std::atomic<bool> failed_{false};
	std::mutex error_mutex_;
	std::string error_;
	/// Repeated while the screen does not change (guarded by error_mutex_)
	core::pFrame last_picture_;
};

} /* namespace mutter_screen */
} /* namespace yuri */
#endif /* MUTTERSCREEN_H_ */
