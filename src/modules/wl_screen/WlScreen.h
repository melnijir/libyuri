/*!
 * @file 		WlScreen.h
 * @author 		Jiri Melnikov
 * @date		2.11.2026
 * @copyright	CESNET, z.s.p.o, 2026
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef WLSCREEN_H_
#define WLSCREEN_H_

#include "yuri/core/thread/IOThread.h"
#include "yuri/core/thread/InputThread.h"
#include <memory>

namespace yuri {
namespace wl_screen {

struct Connection;

/// Captures a Wayland output with the wlr-screencopy protocol (wlroots compositors:
/// labwc on Raspberry Pi OS, wayfire, sway). GNOME and KDE do not offer it.
class WlScreen: public core::IOThread
{
public:
	IOTHREAD_GENERATOR_DECLARATION
	static core::Parameters configure();
	/// Outputs that can be captured (empty without a compositor offering wlr-screencopy)
	static std::vector<core::InputDeviceInfo> enumerate();
	WlScreen(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters);
	virtual ~WlScreen() noexcept;
private:
	virtual void run() override;
	virtual bool step() override;
	virtual bool set_param(const core::Parameter &param) override;

	std::unique_ptr<Connection> conn_;
	std::string display_;
	std::string output_;
	double fps_;
	bool cursor_;
	coordinates_t position_;
	resolution_t resolution_;
};

} /* namespace wl_screen */
} /* namespace yuri */
#endif /* WLSCREEN_H_ */
