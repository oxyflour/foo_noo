#include <thread>

// its wired but we have to include foobar2000 sdk before mongoose header
#include <winsock2.h>

#include "../foobar/foobar2000/SDK/foobar2000.h"
#include "../node/src/node.h"
#include "../node/deps/uv/include/uv.h"

#include "fb_utils.h"
#include "fb_env.h"

#define COMPONENT_NAME "foo_noo"

DECLARE_COMPONENT_VERSION("Foobar2000 NodeJS OO Plugin", "0.0.1", "about message goes here");
VALIDATE_COMPONENT_FILENAME(COMPONENT_NAME ".dll");

void register_napi(uv_async_t *async) {
	_register_napi_fb_env();
	_register_napi_fb_utils();
}

void start_node() {
	uv_async_t async;
	uv_async_init(uv_default_loop(), &async, register_napi);
	uv_async_send(&async);

	char *path = (char *)core_api::get_my_full_path();
	char *script =
		"global.fb_utils = process._linkedBinding('fb_utils');"
		"global.fb_env = process._linkedBinding('fb_env');"

		"const EventEmitter = require('events');"
		"const fb_events = new EventEmitter();"
		"fb_utils.on = fb_events.on.bind(fb_events);"
		"fb_utils.once = fb_events.once.bind(fb_events);"
		"fb_utils.addListener = fb_events.addListener.bind(fb_events);"
		"fb_utils.removeListener = fb_events.removeListener.bind(fb_events);"
		"fb_utils._onmessage = (evt, json) => fb_events.emit(evt, JSON.parse(json));"

		"[process.stdout, process.stderr].forEach(stream => {"
		"    const write = stream.write;"
		"    stream.write = function () {"
		"        write.apply(stream, arguments);"
		"        const args = Array.from(arguments).filter(a => typeof a !== 'function');"
		"        fb_utils.log.apply(null, ['[" COMPONENT_NAME "]'].concat(args));"
		"    };"
		"});"
		"process.on('uncaughtException', err => console.error(err));"

		"const mod_dir = require('path').dirname(fb_env.dll_path);"
		"try {"
		"    console.log('starting module in', mod_dir);"
		"    require(mod_dir);"
		"} catch (err) {"
		"    console.log('startup failed!');"
		"    console.error(err);"
		"}";

	char *argv[] = { path, "-e", script };
	auto argc = sizeof(argv) / sizeof(char *);
	console::print("[" COMPONENT_NAME "] starting nodejs [" NODE_VERSION "] ...");
	node::Start(argc, argv);
}

class app_initquit : public initquit {
public:
	void on_init() {
		std::thread(start_node).detach();
	}
	void on_quit() {
		fb_utils_emit("exit", {});
	}
};

static initquit_factory_t<app_initquit> this_app;
