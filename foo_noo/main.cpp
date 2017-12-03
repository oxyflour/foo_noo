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

char *script =
"	global.fb2k = process._linkedBinding('fb2k')\n"

"	const EventEmitter = require('events')\n"
"	const evts = new EventEmitter()\n"
"	fb2k.emit = evts.emit.bind(evts)\n"
"	fb2k.on = evts.on.bind(evts)\n"
"	fb2k.once = evts.once.bind(evts)\n"
"	fb2k.off = evts.removeListener.bind(evts)\n"

"	const logger = require('fs').createWriteStream('foo_noo.log')\n"
"	;[process.stdout, process.stderr].forEach(stream => {\n"
"	    const write = stream.write\n"
"	    stream.write = function () {\n"
"	        write.apply(stream, arguments)\n"
"			logger.write.apply(logger, arguments)\n"
"	        const args = Array.from(arguments).filter(a => typeof a !== 'function')\n"
"	        fb2k.log.apply(null, ['[" COMPONENT_NAME "]'].concat(args))\n"
"	    }\n"
"	})\n"

"	process.on('unhandledRejection', err => console.error(err))\n"

"	const modDir = require('path').dirname(fb2k.dllPath)\n"
"	try {\n"
"	    console.log('starting module in', modDir)\n"
"	    require(modDir)\n"
"	} catch (err) {\n"
"	    console.log('startup failed!')\n"
"	    console.error(err && err.stack)\n"
"	}\n";

void start_node() {
	_register_napi_fb2k();

	char *path = (char *)core_api::get_my_full_path();
	char *argv[] = { path, "-e", script };
	auto argc = sizeof(argv) / sizeof(char *);

	char options[1024] = { 0 };
	GetEnvironmentVariableA("NODE_OPTIONS", options, sizeof(options));
	console::printf("[" COMPONENT_NAME "] starting nodejs [" NODE_VERSION "] (NODE_OPTIONS: '%s')", options);

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
