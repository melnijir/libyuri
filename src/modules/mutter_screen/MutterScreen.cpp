/*!
 * @file 		MutterScreen.cpp
 * @author 		Jiri Melnikov
 * @date		14.11.2026
 * @copyright	CESNET, z.s.p.o, 2026
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "MutterScreen.h"
#include "yuri/core/Module.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/frame/RawVideoFrame.h"
#include "yuri/core/thread/InputRegister.h"

#include <gio/gio.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/debug/types.h>

#include <algorithm>
#include <cstring>

namespace yuri {
namespace mutter_screen {

IOTHREAD_GENERATOR(MutterScreen)

MODULE_REGISTRATION_BEGIN("mutter_screen")
	REGISTER_IOTHREAD("mutter_screen", MutterScreen)
	REGISTER_INPUT_THREAD("mutter_screen", MutterScreen::enumerate)
MODULE_REGISTRATION_END()

namespace {

const char* const service = "org.gnome.Mutter.ScreenCast";
const char* const service_path = "/org/gnome/Mutter/ScreenCast";
const char* const session_iface = "org.gnome.Mutter.ScreenCast.Session";
const char* const stream_iface = "org.gnome.Mutter.ScreenCast.Stream";

std::string take_error(GError* err)
{
	std::string msg = err ? err->message : "unknown error";
	if (err) g_error_free(err);
	return msg;
}

GVariant* call(GDBusConnection* conn, const char* dest, const char* path, const char* iface, const char* method, GVariant* args, std::string& error)
{
	GError* err = nullptr;
	GVariant* res = g_dbus_connection_call_sync(conn, dest, path, iface, method, args, nullptr,
		G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &err);
	if (!res) error = take_error(err);
	return res;
}

struct Monitor {
	std::string connector;
	std::string name;
	int width = 0;
	int height = 0;
};

/// Monitors of the GNOME session (org.gnome.Mutter.DisplayConfig)
std::vector<Monitor> list_monitors(GDBusConnection* conn)
{
	std::vector<Monitor> monitors;
	std::string error;
	GVariant* res = call(conn, "org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
		"org.gnome.Mutter.DisplayConfig", "GetCurrentState", nullptr, error);
	if (!res) return monitors;
	// (u serial, a((ssss)a(siiddada{sv})a{sv}) monitors, a(iiduba(ssss)a{sv}) logical, a{sv})
	GVariant* list = g_variant_get_child_value(res, 1);
	for (gsize i = 0; i < g_variant_n_children(list); ++i) {
		GVariant* mon = g_variant_get_child_value(list, i);
		GVariant* spec = g_variant_get_child_value(mon, 0);
		const gchar *connector = nullptr, *vendor = nullptr, *product = nullptr, *serial = nullptr;
		g_variant_get(spec, "(&s&s&s&s)", &connector, &vendor, &product, &serial);
		Monitor m;
		m.connector = connector ? connector : "";
		GVariant* modes = g_variant_get_child_value(mon, 1);
		for (gsize j = 0; j < g_variant_n_children(modes); ++j) {
			GVariant* mode = g_variant_get_child_value(modes, j);
			gint32 w = 0, h = 0;
			g_variant_get_child(mode, 1, "i", &w);
			g_variant_get_child(mode, 2, "i", &h);
			GVariant* props = g_variant_get_child_value(mode, 6);
			gboolean current = FALSE;
			g_variant_lookup(props, "is-current", "b", &current);
			if (current || (!m.width && !m.height)) { m.width = w; m.height = h; }
			g_variant_unref(props);
			g_variant_unref(mode);
		}
		GVariant* props = g_variant_get_child_value(mon, 2);
		const gchar* display_name = nullptr;
		if (g_variant_lookup(props, "display-name", "&s", &display_name) && display_name) m.name = display_name;
		else m.name = std::string(vendor ? vendor : "") + " " + (product ? product : "");
		g_variant_unref(props);
		g_variant_unref(modes);
		g_variant_unref(spec);
		g_variant_unref(mon);
		monitors.push_back(m);
	}
	g_variant_unref(list);
	g_variant_unref(res);
	return monitors;
}

bool has_screencast(GDBusConnection* conn)
{
	std::string error;
	GVariant* res = call(conn, service, service_path, "org.freedesktop.DBus.Properties", "Get",
		g_variant_new("(ss)", service, "Version"), error);
	if (!res) return false;
	g_variant_unref(res);
	return true;
}

} // namespace

/// A screen cast session of Mutter; it lives as long as our D-Bus connection
struct Session {
	GMainContext* context = nullptr;
	GDBusConnection* conn = nullptr;
	std::string session_path;
	std::string stream_path;
	guint added_id = 0;
	guint closed_id = 0;
	uint32_t node_id = 0;
	bool have_node = false;
	bool closed = false;

	~Session()
	{
		if (conn) {
			if (!session_path.empty() && !closed) {
				std::string error;
				if (auto r = call(conn, service, session_path.c_str(), session_iface, "Stop", nullptr, error)) g_variant_unref(r);
			}
			if (added_id) g_dbus_connection_signal_unsubscribe(conn, added_id);
			if (closed_id) g_dbus_connection_signal_unsubscribe(conn, closed_id);
			g_dbus_connection_close_sync(conn, nullptr, nullptr);
			g_object_unref(conn);
		}
		if (context) g_main_context_unref(context);
	}

	static void on_added(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer data)
	{
		auto* s = static_cast<Session*>(data);
		g_variant_get(params, "(u)", &s->node_id);
		s->have_node = true;
	}
	static void on_closed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, gpointer data)
	{
		static_cast<Session*>(data)->closed = true;
	}

	/// Creates the session and starts recording a monitor ("" = the primary one)
	void start(const std::string& monitor, bool cursor)
	{
		context = g_main_context_new();
		g_main_context_push_thread_default(context);
		std::string error;
		GError* err = nullptr;
		// a private connection: the session ends with it, nothing else in the process is affected
		gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &err);
		if (address) {
			conn = g_dbus_connection_new_for_address_sync(address,
				static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
				nullptr, nullptr, &err);
			g_free(address);
		}
		auto fail = [&](const std::string& what) {
			g_main_context_pop_thread_default(context);
			throw exception::InitializationFailed(what);
		};
		if (!conn) fail("No D-Bus session bus: " + take_error(err));
		if (!has_screencast(conn)) fail("No GNOME screen cast service (org.gnome.Mutter.ScreenCast): this desktop is not GNOME");

		GVariantBuilder props;
		g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
		GVariant* res = call(conn, service, service_path, service, "CreateSession", g_variant_new("(a{sv})", &props), error);
		if (!res) fail("Creating a screen cast session failed: " + error);
		const gchar* path = nullptr;
		g_variant_get(res, "(&o)", &path);
		session_path = path;
		g_variant_unref(res);
		closed_id = g_dbus_connection_signal_subscribe(conn, service, session_iface, "Closed", session_path.c_str(),
			nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &Session::on_closed, this, nullptr);

		g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
		// 0 hidden, 1 embedded in the picture, 2 as metadata
		g_variant_builder_add(&props, "{sv}", "cursor-mode", g_variant_new_uint32(cursor ? 1 : 0));
		g_variant_builder_add(&props, "{sv}", "is-recording", g_variant_new_boolean(TRUE));
		res = call(conn, service, session_path.c_str(), session_iface, "RecordMonitor",
			g_variant_new("(sa{sv})", monitor.c_str(), &props), error);
		if (!res) fail(monitor.empty() ? "Recording the monitor failed: " + error : "Recording monitor " + monitor + " failed: " + error);
		g_variant_get(res, "(&o)", &path);
		stream_path = path;
		g_variant_unref(res);
		added_id = g_dbus_connection_signal_subscribe(conn, service, stream_iface, "PipeWireStreamAdded", stream_path.c_str(),
			nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &Session::on_added, this, nullptr);

		res = call(conn, service, session_path.c_str(), session_iface, "Start", nullptr, error);
		if (!res) fail("Starting the screen cast failed: " + error);
		g_variant_unref(res);
		const gint64 until = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
		while (!have_node && !closed && g_get_monotonic_time() < until)
			if (!g_main_context_iteration(context, FALSE)) g_usleep(10000);
		g_main_context_pop_thread_default(context);
		if (!have_node) throw exception::InitializationFailed("GNOME did not provide the screen cast stream");
	}

	/// Delivers pending D-Bus signals (session closed)
	void poll()
	{
		if (!context) return;
		g_main_context_push_thread_default(context);
		while (g_main_context_iteration(context, FALSE)) {}
		g_main_context_pop_thread_default(context);
	}
};

/// The PipeWire video stream of the screen cast
struct Stream {
	MutterScreen* owner = nullptr;
	pw_thread_loop* loop = nullptr;
	pw_context* context = nullptr;
	pw_core* core = nullptr;
	pw_stream* stream = nullptr;
	spa_hook listener{};
	spa_video_info_raw format{};
	bool have_format = false;

	~Stream()
	{
		if (loop) pw_thread_loop_stop(loop);
		if (stream) pw_stream_destroy(stream);
		if (core) pw_core_disconnect(core);
		if (context) pw_context_destroy(context);
		if (loop) pw_thread_loop_destroy(loop);
	}

	static void on_param_changed(void* data, uint32_t id, const spa_pod* param)
	{
		auto* s = static_cast<Stream*>(data);
		if (!param || id != SPA_PARAM_Format) return;
		uint32_t media_type = 0, media_subtype = 0;
		if (spa_format_parse(param, &media_type, &media_subtype) < 0) return;
		if (media_type != SPA_MEDIA_TYPE_video || media_subtype != SPA_MEDIA_SUBTYPE_raw) return;
		if (spa_format_video_raw_parse(param, &s->format) < 0) return;
		s->have_format = true;
		// memory the CPU can read (no DMA-BUF)
		uint8_t buffer[1024];
		spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
		const spa_pod* params[1];
		params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b,
			SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
			SPA_PARAM_BUFFERS_dataType, SPA_POD_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))));
		pw_stream_update_params(s->stream, params, 1);
	}

	static void on_state_changed(void* data, pw_stream_state, pw_stream_state state, const char* error)
	{
		auto* s = static_cast<Stream*>(data);
		if (state == PW_STREAM_STATE_ERROR) s->owner->on_stream_error(std::string("Screen cast stream failed: ") + (error ? error : ""));
		if (state == PW_STREAM_STATE_UNCONNECTED) s->owner->on_stream_error("Screen cast stream ended");
	}

	static void on_process(void* data)
	{
		auto* s = static_cast<Stream*>(data);
		pw_buffer* b = pw_stream_dequeue_buffer(s->stream);
		if (!b) return;
		spa_buffer* buf = b->buffer;
		const auto& d = buf->datas[0];
		// buffers with only cursor or damage metadata carry no picture
		if (s->have_format && d.data && d.chunk && d.chunk->size > 0) {
			const uint32_t w = s->format.size.width, h = s->format.size.height;
			const uint32_t stride = d.chunk->stride > 0 ? static_cast<uint32_t>(d.chunk->stride) : w * 4;
			const auto* pixels = static_cast<const uint8_t*>(d.data) + d.chunk->offset;
			const auto f = s->format.format;
			const bool bgr = f == SPA_VIDEO_FORMAT_BGRx || f == SPA_VIDEO_FORMAT_BGRA;
			const bool opaque = f == SPA_VIDEO_FORMAT_BGRx || f == SPA_VIDEO_FORMAT_RGBx;
			if (static_cast<size_t>(stride) * (h - 1) + w * 4 <= d.maxsize - d.chunk->offset)
				s->owner->on_frame(pixels, w, h, stride, bgr, opaque);
		}
		pw_stream_queue_buffer(s->stream, b);
	}

	void start(uint32_t node_id)
	{
		static const pw_stream_events events = [] {
			pw_stream_events e{};
			e.version = PW_VERSION_STREAM_EVENTS;
			e.state_changed = &Stream::on_state_changed;
			e.param_changed = &Stream::on_param_changed;
			e.process = &Stream::on_process;
			return e;
		}();
		pw_init(nullptr, nullptr);
		loop = pw_thread_loop_new("mutter-screen", nullptr);
		if (!loop) throw exception::InitializationFailed("PipeWire: no thread loop");
		context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
		if (!context) throw exception::InitializationFailed("PipeWire: no context");
		if (pw_thread_loop_start(loop) < 0) throw exception::InitializationFailed("PipeWire: thread loop did not start");
		pw_thread_loop_lock(loop);
		core = pw_context_connect(context, nullptr, 0);
		if (!core) {
			pw_thread_loop_unlock(loop);
			throw exception::InitializationFailed("PipeWire is not running");
		}
		stream = pw_stream_new(core, "dicaffeine-screen", pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
		pw_stream_add_listener(stream, &listener, &events, this);

		uint8_t buffer[1024];
		spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
		const spa_rectangle def_size = SPA_RECTANGLE(1920, 1080), min_size = SPA_RECTANGLE(1, 1), max_size = SPA_RECTANGLE(16384, 16384);
		const spa_fraction def_rate = SPA_FRACTION(30, 1), min_rate = SPA_FRACTION(0, 1), max_rate = SPA_FRACTION(1000, 1);
		const spa_pod* params[1];
		params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b,
			SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
			SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
			SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
			SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
				SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA),
			SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def_size, &min_size, &max_size),
			SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&def_rate, &min_rate, &max_rate)));
		const int res = pw_stream_connect(stream, PW_DIRECTION_INPUT, node_id,
			static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
		pw_thread_loop_unlock(loop);
		if (res < 0) throw exception::InitializationFailed("PipeWire: connecting to the screen cast failed");
	}
};

core::Parameters MutterScreen::configure()
{
	core::Parameters p = core::IOThread::configure();
	p.set_description("Screen capture of a GNOME desktop (Mutter screen cast through PipeWire)");
	p["monitor"]["Monitor to capture by connector, e.g. HDMI-1 or eDP-1 (empty = the primary one)"]=std::string();
	p["fps"]["Frames per second; a still screen is repeated at this rate (0 = only changes)"]=30.0;
	p["cursor"]["Capture the mouse cursor as well"]=true;
	p["position"]["Position of the captured area in pixels"]="0x0";
	p["resolution"]["Size of the captured area (0x0 = the whole monitor)"]="0x0";
	return p;
}

std::vector<core::InputDeviceInfo> MutterScreen::enumerate()
{
	std::vector<core::InputDeviceInfo> devices;
	GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
	if (!conn) return devices;
	if (has_screencast(conn)) {
		for (const auto& m: list_monitors(conn)) {
			core::InputDeviceInfo device;
			device.device_name = m.name.empty() ? m.connector : m.name + " (" + m.connector + ")";
			device.main_param_order = {"monitor"};
			core::InputDeviceConfig cfg;
			cfg.params["monitor"] = m.connector;
			cfg.resolution = {static_cast<dimension_t>(m.width), static_cast<dimension_t>(m.height)};
			device.configurations.push_back(std::move(cfg));
			devices.push_back(std::move(device));
		}
	}
	g_object_unref(conn);
	return devices;
}

MutterScreen::MutterScreen(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
core::IOThread(log_, parent, 0, 1, std::string("mutter_screen")), fps_(30.0), cursor_(true),
position_{0, 0}, resolution_{0, 0}
{
	IOTHREAD_INIT(parameters)
	session_ = std::make_unique<Session>();
	session_->start(monitor_, cursor_);
	stream_ = std::make_unique<Stream>();
	stream_->owner = this;
	stream_->start(session_->node_id);
	log[log::info] << "Capturing " << (monitor_.empty() ? std::string("the primary monitor") : "monitor " + monitor_)
		<< " of the GNOME desktop (PipeWire node " << session_->node_id << ")";
}

MutterScreen::~MutterScreen() noexcept
{
	stream_.reset();
	session_.reset();
}

void MutterScreen::on_frame(const uint8_t* data, uint32_t width, uint32_t height, uint32_t stride, bool bgr, bool opaque)
{
	std::lock_guard<std::mutex> lock(error_mutex_);
	timestamp_t now;
	if (fps_ > 0.0 && !first_frame_ && now - last_frame_ < 1_s / fps_ * 0.9) return;
	const uint32_t x0 = std::min<uint32_t>(position_.x, width);
	const uint32_t y0 = std::min<uint32_t>(position_.y, height);
	const uint32_t w = resolution_.width ? std::min<uint32_t>(resolution_.width, width - x0) : width - x0;
	const uint32_t h = resolution_.height ? std::min<uint32_t>(resolution_.height, height - y0) : height - y0;
	if (!w || !h) return;
	auto frame = core::RawVideoFrame::create_empty(bgr ? core::raw_format::bgra32 : core::raw_format::rgba32,
		{static_cast<dimension_t>(w), static_cast<dimension_t>(h)}, true);
	uint8_t* out = PLANE_RAW_DATA(frame, 0);
	for (uint32_t line = 0; line < h; ++line) {
		std::memcpy(out, data + static_cast<size_t>(y0 + line) * stride + x0 * 4, w * 4);
		if (opaque) for (uint32_t i = 3; i < w * 4; i += 4) out[i] = 0xff;
		out += w * 4;
	}
	if (first_frame_) log[log::info] << "First picture: " << width << "x" << height;
	first_frame_ = false;
	last_frame_ = now;
	push_frame(0, frame);
	last_picture_ = frame;
}

void MutterScreen::on_stream_error(const std::string& message)
{
	std::lock_guard<std::mutex> lock(error_mutex_);
	if (error_.empty()) error_ = message;
	failed_ = true;
}

void MutterScreen::run()
{
	while (still_running()) {
		session_->poll();
		if (session_->closed) {
			log[log::error] << "GNOME closed the screen cast session";
			request_end(core::yuri_exit_interrupted);
			break;
		}
		if (failed_) {
			std::lock_guard<std::mutex> lock(error_mutex_);
			log[log::error] << error_;
			request_end(core::yuri_exit_interrupted);
			break;
		}
		// GNOME sends a picture only when something changes: a still screen is repeated
		if (fps_ > 0.0) {
			timestamp_t now;
			std::lock_guard<std::mutex> lock(error_mutex_);
			if (!first_frame_ && last_picture_ && now - last_frame_ > 1_s / fps_) {
				last_frame_ = now;
				push_frame(0, last_picture_);
			}
		}
		sleep(5_ms);
	}
	close_pipes();
	stream_.reset();
	session_.reset();
}

bool MutterScreen::set_param(const core::Parameter &param)
{
	if (assign_parameters(param)
			(monitor_, "monitor")
			(fps_, "fps")
			(cursor_, "cursor")
			(position_, "position")
			(resolution_, "resolution"))
		return true;
	return core::IOThread::set_param(param);
}

} /* namespace mutter_screen */
} /* namespace yuri */
