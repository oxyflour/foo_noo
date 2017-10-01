#include <thread>

// its weird but we have to include this before foobar2000 SDK and libuv
#include <winsock2.h>

#include "../foobar/foobar2000/SDK/foobar2000.h"
#include "../node/src/node.h"
#include "../node/deps/uv/include/uv.h"

#include "fb2k.h"

#define COMPONENT_NAME "foo_noo"

DECLARE_COMPONENT_VERSION("Foobar2000 NodeJS OO Plugin", "0.0.1", "about message goes here");
VALIDATE_COMPONENT_FILENAME(COMPONENT_NAME ".dll");

void register_napi(uv_async_t *async) {
	_register_napi_fb2k();
}

void start_node() {
	uv_async_t async;
	uv_async_init(uv_default_loop(), &async, register_napi);
	uv_async_send(&async);

	char *path = (char *)core_api::get_my_full_path();
	char *script =
		"global.fb2k = process._linkedBinding('fb_utils');"

		"const EventEmitter = require('events');"
		"const fbEvents = new EventEmitter();"
		"fb2k._onmessage = (evt, data) => fbEvents.emit(evt, data);"
		"fb2k.on = fbEvents.on.bind(fbEvents);"
		"fb2k.off = fbEvents.off.bind(fbEvents);"
		"fb2k.once = fbEvents.once.bind(fbEvents);"

		"fb2k.dumpLibrary = cb => fb2k.once(fb2k.send('library:dump'), cb)"

		"[process.stdout, process.stderr].forEach(stream => {"
		"    const write = stream.write;"
		"    stream.write = function () {"
		"        write.apply(stream, arguments);"
		"        const args = Array.from(arguments).filter(a => typeof a !== 'function');"
		"        fb2k.log.apply(null, ['[" COMPONENT_NAME "]'].concat(args));"
		"    };"
		"});"
		"process.on('uncaughtException', err => console.error(err));"

		"const modDir = require('path').dirname(fb2k.dllPath);"
		"try {"
		"    console.log('starting module in', modDir);"
		"    require(modDir);"
		"} catch (err) {"
		"    console.log('startup failed!');"
		"    console.error(err);"
		"}";

	char *argv[] = { path, "--inspect", "-e", script };
	auto argc = sizeof(argv) / sizeof(char *);
	console::print("[" COMPONENT_NAME "] starting nodejs [" NODE_VERSION "] ...");
	node::Start(argc, argv);
}

class playback_listener : public play_callback_impl_base {
public:
	void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {
		static_api_ptr_t<playback_control> pc;
		fb2k_emit("play:start", {
			{ "currentTime", pc->playback_get_position() },
		});
	}
	void on_playback_new_track(metadb_handle_ptr p_track) {
		static_api_ptr_t<playback_control> pc;
		fb2k_emit("play:meta", {
			{ "duration", p_track->get_length() },
			{ "currentTime", pc->playback_get_position() },
			{ "volume", pc->get_volume() },
			{ "paused", pc->is_paused() },
			{ "stopped", !pc->is_playing() },
		});
	}
	void on_playback_stop(play_control::t_stop_reason p_reason) {
		auto evt = p_reason == play_control::t_stop_reason::stop_reason_eof ? "play:ended" : "play:stop";
		fb2k_emit(evt, {
		});
	}
	void on_playback_pause(bool p_state) {
		static_api_ptr_t<playback_control> pc;
		auto evt = p_state ? "play:pause" : "play:start";
		fb2k_emit(evt, {
			{ "currentTime", pc->playback_get_position() },
		});
	}
	void on_volume_change(float p_new_val) {
		fb2k_emit("play:volume", {
			{ "volume", p_new_val },
		});
	}
	void on_playback_seek(double p_time) {
		fb2k_emit("play:seek", {
			{ "currentTime", p_time },
		});
	}
};

class library_listener : public library_callback_dynamic_impl_base {
public:
	static nlohmann::json meta_list_to_json(const pfc::list_base_const_t<metadb_handle_ptr> &list) {
		static_api_ptr_t<library_manager> lib;
		auto arr = nlohmann::json::array();
		for (size_t i = 0, n = list.get_count(); i < n; i++) {
			auto item = list.get_item(i);
			auto json = meta_to_json(item, lib);
			arr.push_back(json);
		}
		return arr;
	}
	void on_items_added(const pfc::list_base_const_t<metadb_handle_ptr> &list) {
		fb2k_emit("library:add", {
			{ "type", "library:add" },
			{ "list", library_listener::meta_list_to_json(list) },
		});
	}
	void on_items_removed(const pfc::list_base_const_t<metadb_handle_ptr> &list) {
		fb2k_emit("library:remove", {
			{ "type", "library:remove" },
			{ "list", library_listener::meta_list_to_json(list) },
		});
	}
	void on_items_modified(const pfc::list_base_const_t<metadb_handle_ptr> &list) {
		fb2k_emit("library:update", {
			{ "type", "library:update" },
			{ "list", library_listener::meta_list_to_json(list) },
		});
	}
};

class app_initquit : public initquit {
private:
	playback_listener *pl;
	library_listener *ll;
public:
	void on_init() {
		pl = new playback_listener();
		ll = new library_listener();
		std::thread(start_node).detach();
	}
	void on_quit() {
		fb2k_emit("exit", {});
		delete pl;
		delete ll;
	}
};

static initquit_factory_t<app_initquit> this_app;
