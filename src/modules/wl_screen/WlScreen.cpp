/*!
 * @file 		WlScreen.cpp
 * @author 		Jiri Melnikov
 * @date		2.11.2026
 * @copyright	CESNET, z.s.p.o, 2026
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "WlScreen.h"
#include "yuri/core/Module.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/frame/RawVideoFrame.h"
#include "yuri/core/thread/InputRegister.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include <wayland-client.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>

namespace yuri {
namespace wl_screen {

IOTHREAD_GENERATOR(WlScreen)

MODULE_REGISTRATION_BEGIN("wl_screen")
	REGISTER_IOTHREAD("wl_screen", WlScreen)
	REGISTER_INPUT_THREAD("wl_screen", WlScreen::enumerate)
MODULE_REGISTRATION_END()

core::Parameters WlScreen::configure()
{
	core::Parameters p = core::IOThread::configure();
	p.set_description("Captures a Wayland output (wlr-screencopy: labwc, wayfire, sway).");
	p["display"]["Wayland display (empty = $WAYLAND_DISPLAY; X11 names like :0 are ignored)"]=std::string();
	p["output"]["Output to capture, e.g. HDMI-A-1 (empty = the first one)"]=std::string();
	p["fps"]["Frames per second (0 = as fast as possible)"]=0.0;
	p["cursor"]["Capture the mouse cursor as well"]=false;
	p["position"]["Position of the captured area in pixels"]="0x0";
	p["resolution"]["Size of the captured area (0x0 = the whole output)"]="0x0";
	return p;
}

struct Output {
	wl_output* output = nullptr;
	/// registry name, to notice when the output goes away (display unplugged)
	uint32_t global = 0;
	bool removed = false;
	std::string name;
	std::string description;
	int32_t width = 0;
	int32_t height = 0;
};

/// One captured frame: what the compositor announced and how the copy ended
struct Capture {
	uint32_t format = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t stride = 0;
	bool have_format = false;
	bool buffer_done = false;
	bool ready = false;
	bool failed = false;
	uint32_t flags = 0;
};

struct Connection {
	wl_display* display = nullptr;
	wl_registry* registry = nullptr;
	wl_shm* shm = nullptr;
	zwlr_screencopy_manager_v1* manager = nullptr;
	uint32_t manager_version = 0;
	std::vector<std::unique_ptr<Output>> outputs;
	Output* current = nullptr;
	// shared memory buffer the compositor copies into, reused while the size stays
	wl_buffer* buffer = nullptr;
	void* data = nullptr;
	size_t size = 0;
	uint32_t buf_format = 0, buf_width = 0, buf_height = 0, buf_stride = 0;

	~Connection() {
		release_buffer();
		for (auto& o: outputs) if (o->output) wl_output_destroy(o->output);
		if (manager) zwlr_screencopy_manager_v1_destroy(manager);
		if (shm) wl_shm_destroy(shm);
		if (registry) wl_registry_destroy(registry);
		if (display) wl_display_disconnect(display);
	}
	void release_buffer() {
		if (buffer) wl_buffer_destroy(buffer);
		if (data) munmap(data, size);
		buffer = nullptr;
		data = nullptr;
		size = 0;
	}
};

namespace {

// ---------------------------------------------------------------- wl_output
void output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
void output_mode(void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t) {
	if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
	auto o = static_cast<Output*>(data);
	o->width = width;
	o->height = height;
}
void output_done(void*, wl_output*) {}
void output_scale(void*, wl_output*, int32_t) {}
void output_name(void* data, wl_output*, const char* name) { static_cast<Output*>(data)->name = name; }
void output_description(void* data, wl_output*, const char* d) { static_cast<Output*>(data)->description = d; }
const wl_output_listener output_listener = {
	output_geometry, output_mode, output_done, output_scale, output_name, output_description,
};

// ---------------------------------------------------------------- registry
void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
	auto c = static_cast<Connection*>(data);
	if (std::strcmp(interface, wl_shm_interface.name) == 0) {
		c->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
	} else if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
		c->manager_version = std::min<uint32_t>(version, 3);
		c->manager = static_cast<zwlr_screencopy_manager_v1*>(
			wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, c->manager_version));
	} else if (std::strcmp(interface, wl_output_interface.name) == 0) {
		auto o = std::make_unique<Output>();
		o->global = name;
		// version 4 tells the output's name (HDMI-A-1)
		o->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min<uint32_t>(version, 4)));
		wl_output_add_listener(o->output, &output_listener, o.get());
		c->outputs.push_back(std::move(o));
	}
}
void registry_global_remove(void* data, wl_registry*, uint32_t name) {
	for (auto& o: static_cast<Connection*>(data)->outputs) if (o->global == name) o->removed = true;
}
const wl_registry_listener registry_listener = { registry_global, registry_global_remove };

// ---------------------------------------------------------------- screencopy frame
bool usable_format(uint32_t format) {
	return format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888
		|| format == WL_SHM_FORMAT_XBGR8888 || format == WL_SHM_FORMAT_ABGR8888;
}
void frame_buffer(void* data, zwlr_screencopy_frame_v1*, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
	auto c = static_cast<Capture*>(data);
	// several formats may be offered (version 3): take the first 8-bit RGB one
	if (c->have_format || !usable_format(format)) return;
	c->format = format;
	c->width = width;
	c->height = height;
	c->stride = stride;
	c->have_format = true;
}
void frame_flags(void* data, zwlr_screencopy_frame_v1*, uint32_t flags) { static_cast<Capture*>(data)->flags = flags; }
void frame_ready(void* data, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) { static_cast<Capture*>(data)->ready = true; }
void frame_failed(void* data, zwlr_screencopy_frame_v1*) { static_cast<Capture*>(data)->failed = true; }
void frame_damage(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t, uint32_t) {}
void frame_linux_dmabuf(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) {}
void frame_buffer_done(void* data, zwlr_screencopy_frame_v1*) { static_cast<Capture*>(data)->buffer_done = true; }
const zwlr_screencopy_frame_v1_listener frame_listener = {
	frame_buffer, frame_flags, frame_ready, frame_failed, frame_damage, frame_linux_dmabuf, frame_buffer_done,
};

/// Connects and reads the globals; nullptr when there is no Wayland compositor
std::unique_ptr<Connection> connect(const std::string& display) {
	auto c = std::make_unique<Connection>();
	// ":0" is an X11 display name (a route made for X11): use the session's Wayland display
	const bool own = !display.empty() && display[0] != ':';
	c->display = wl_display_connect(own ? display.c_str() : nullptr);
	if (!c->display) return nullptr;
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, c.get());
	wl_display_roundtrip(c->display);  // globals
	wl_display_roundtrip(c->display);  // output names and modes
	return c;
}

bool ensure_buffer(Connection& c, const Capture& cap) {
	if (c.buffer && c.buf_format == cap.format && c.buf_width == cap.width
			&& c.buf_height == cap.height && c.buf_stride == cap.stride) return true;
	c.release_buffer();
	const size_t size = static_cast<size_t>(cap.stride) * cap.height;
	const int fd = memfd_create("yuri-wl-screen", MFD_CLOEXEC);
	if (fd < 0) return false;
	if (ftruncate(fd, size) < 0) { close(fd); return false; }
	void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) { close(fd); return false; }
	wl_shm_pool* pool = wl_shm_create_pool(c.shm, fd, size);
	c.buffer = wl_shm_pool_create_buffer(pool, 0, cap.width, cap.height, cap.stride, cap.format);
	wl_shm_pool_destroy(pool);
	close(fd);
	c.data = data;
	c.size = size;
	c.buf_format = cap.format;
	c.buf_width = cap.width;
	c.buf_height = cap.height;
	c.buf_stride = cap.stride;
	return true;
}

/// Output to capture: the wanted one (by name) or the first; outputs that went away are dropped.
/// When the display is unplugged labwc replaces it by a headless output (NOOP-1), which is taken then.
Output* pick_output(Connection& c, const std::string& wanted) {
	for (auto it = c.outputs.begin(); it != c.outputs.end();) {
		if (!(*it)->removed) { ++it; continue; }
		if (c.current == it->get()) c.current = nullptr;
		wl_output_destroy((*it)->output);
		it = c.outputs.erase(it);
	}
	for (auto& o: c.outputs) if (wanted.empty() || o->name == wanted) return o.get();
	return nullptr;
}

const char* NO_SCREENCOPY = "The compositor does not offer wlr-screencopy (works with labwc, wayfire, sway; not with GNOME or KDE)";

}

std::vector<core::InputDeviceInfo> WlScreen::enumerate()
{
	std::vector<core::InputDeviceInfo> devices;
	auto c = connect({});
	if (!c || !c->manager || !c->shm) return devices;
	for (const auto& o: c->outputs) {
		core::InputDeviceInfo device;
		device.device_name = o->description.empty() ? o->name : o->description;
		device.main_param_order = {"output"};
		core::InputDeviceConfig cfg;
		cfg.params["output"] = o->name;
		cfg.resolution = {static_cast<dimension_t>(o->width), static_cast<dimension_t>(o->height)};
		device.configurations.push_back(std::move(cfg));
		devices.push_back(std::move(device));
	}
	return devices;
}

WlScreen::WlScreen(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
core::IOThread(log_, parent, 0, 1, std::string("wl_screen")), fps_(0.0), cursor_(false),
position_{0, 0}, resolution_{0, 0}
{
	IOTHREAD_INIT(parameters)
	conn_ = connect(display_);
	if (!conn_) throw exception::InitializationFailed("No Wayland display to connect to");
	if (!conn_->manager || !conn_->shm) throw exception::InitializationFailed(NO_SCREENCOPY);
	if (conn_->outputs.empty()) throw exception::InitializationFailed("The compositor has no output to capture");
	conn_->current = pick_output(*conn_, output_);
	if (!conn_->current) {
		std::string names;
		for (const auto& o: conn_->outputs) names += (names.empty() ? "" : ", ") + o->name;
		throw exception::InitializationFailed("Output " + output_ + " not found (outputs: " + names + ")");
	}
	const auto& o = *conn_->current;
	log[log::info] << "Capturing output " << o.name << " (" << o.description << "), " << o.width << "x" << o.height;
}

WlScreen::~WlScreen() noexcept
{
}

void WlScreen::run()
{
	timestamp_t last_time;
	const duration_t delta = fps_ > 0.0 ? 1_s / fps_ : 0_s;
	while (still_running()) {
		if (fps_ > 0.0) {
			timestamp_t now;
			const auto since = now - last_time;
			if (since < delta) {
				sleep((delta - since) / 2.0);
				continue;
			}
			last_time += delta;
		}
		if (!step()) {
			// nothing more will come: end the route instead of sending nothing
			request_end(core::yuri_exit_interrupted);
			break;
		}
	}
	close_pipes();
	conn_.reset();
}

bool WlScreen::step()
{
	Connection& c = *conn_;
	if (!c.current || c.current->removed) {
		// the output went away (display unplugged): take another one, or wait for the wanted one
		wl_display_roundtrip(c.display);
		wl_display_roundtrip(c.display);
		Output* next = pick_output(c, output_);
		if (!next) {
			if (c.current || !waiting_) log[log::warning] << (output_.empty() ? std::string("No output to capture") : "Output " + output_ + " is gone")
				<< ", waiting for one";
			c.current = nullptr;
			waiting_ = true;
			sleep(500_ms);
			return wl_display_get_error(c.display) == 0;
		}
		c.current = next;
		waiting_ = false;
		log[log::info] << "Capturing output " << next->name << " (" << next->description << "), " << next->width << "x" << next->height;
	}
	Capture cap;
	auto frame = zwlr_screencopy_manager_v1_capture_output(c.manager, cursor_ ? 1 : 0, c.current->output);
	zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, &cap);
	// version 3 announces all buffer types and ends with buffer_done; older ones send one buffer event
	auto announced = [&] { return cap.failed || (c.manager_version >= 3 ? cap.buffer_done : cap.have_format); };
	while (!announced() && wl_display_dispatch(c.display) != -1) {}
	if (!cap.failed && cap.have_format && ensure_buffer(c, cap)) {
		zwlr_screencopy_frame_v1_copy(frame, c.buffer);
		while (!cap.ready && !cap.failed && wl_display_dispatch(c.display) != -1) {}
	}
	zwlr_screencopy_frame_v1_destroy(frame);
	if (wl_display_get_error(c.display)) {
		log[log::error] << "Connection to the Wayland compositor lost";
		return false;
	}
	if (!cap.ready) {
		// usually the output is just going away: its removal comes with the next events
		wl_display_roundtrip(c.display);
		if (c.current->removed) return true;
		if (!cap.failed && !cap.have_format) {
			log[log::error] << "The compositor offers no 8-bit RGB buffer to copy into";
			return false;
		}
		log[log::warning] << "Capturing output " << c.current->name << " failed";
		sleep(100_ms);
		return true;
	}

	// crop to position/resolution
	const uint32_t x0 = std::min<uint32_t>(position_.x, cap.width);
	const uint32_t y0 = std::min<uint32_t>(position_.y, cap.height);
	const uint32_t w = resolution_.width ? std::min<uint32_t>(resolution_.width, cap.width - x0) : cap.width - x0;
	const uint32_t h = resolution_.height ? std::min<uint32_t>(resolution_.height, cap.height - y0) : cap.height - y0;
	if (!w || !h) return true;

	// XRGB8888 is B,G,R,X in memory; the X byte is undefined, so it is set opaque
	const bool bgr = cap.format == WL_SHM_FORMAT_XRGB8888 || cap.format == WL_SHM_FORMAT_ARGB8888;
	const bool opaque = cap.format == WL_SHM_FORMAT_XRGB8888 || cap.format == WL_SHM_FORMAT_XBGR8888;
	const bool y_invert = cap.flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
	auto out_frame = core::RawVideoFrame::create_empty(bgr ? core::raw_format::bgra32 : core::raw_format::rgba32,
		{static_cast<dimension_t>(w), static_cast<dimension_t>(h)}, true);
	uint8_t* out = PLANE_RAW_DATA(out_frame, 0);
	const auto* src = static_cast<const uint8_t*>(c.data);
	for (uint32_t line = 0; line < h; ++line) {
		const uint32_t src_line = y_invert ? cap.height - 1 - (y0 + line) : y0 + line;
		const uint8_t* from = src + static_cast<size_t>(src_line) * cap.stride + x0 * 4;
		std::memcpy(out, from, w * 4);
		if (opaque) for (uint32_t i = 3; i < w * 4; i += 4) out[i] = 0xff;
		out += w * 4;
	}
	push_frame(0, out_frame);
	return true;
}

bool WlScreen::set_param(const core::Parameter &param)
{
	if (assign_parameters(param)
			(display_, "display")
			(output_, "output")
			(fps_, "fps")
			(cursor_, "cursor")
			(position_, "position")
			(resolution_, "resolution"))
		return true;
	return core::IOThread::set_param(param);
}

} /* namespace wl_screen */
} /* namespace yuri */
