#include <queue>

// its wired but we have to include foobar2000 sdk before mongoose header
#include <winsock2.h>

#include "fb_utils.h"
#include "../foobar/foobar2000/SDK/foobar2000.h"
#include "../node/deps/uv/include/uv.h"
#include "../node/src/node_api.h"

napi_value fb_utils_log(napi_env env, napi_callback_info info) {
	size_t argc = 16;
	napi_value args[16];
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) == napi_ok) {
		size_t offset = 0;
		char buffer[4096] = { 0 };
		for (auto i = 0; i < argc; i++) {
			napi_value arg;
			size_t len = 0;
			char str[1024] = { 0 };
			if (napi_coerce_to_string(env, args[i], &arg) == napi_ok &&
				napi_get_value_string_utf8(env, arg, str, sizeof(str), &len) == napi_ok) {
				offset += sprintf(buffer + offset, offset == 0 ? "%s" : " %s", str);
			}
		}
		console::print(buffer);
	}
	return nullptr;
}

struct async_msg {
	std::string evt;
	std::string data;
};

struct async_evt_t {
	napi_async_work work;
	napi_ref obj;
	HANDLE evt;
	std::queue<async_msg> queue;
};

async_evt_t async;

void fb_utils_emit(const char *evt, std::initializer_list<json> init) {
	async_msg msg;
	msg.evt = std::string(evt);
	msg.data = json(init).dump();
	async.queue.push(msg);
	SetEvent(async.evt);
}

void execute_callback(napi_env env, void* data) {
	WaitForSingleObject(async.evt, INFINITE);
}

void complete_callback(napi_env env, napi_status status, void* data) {
	napi_value obj;
	napi_value key;
	napi_value cb;
	napi_valuetype cb_type;
	if (napi_get_reference_value(env, async.obj, &obj) == napi_ok &&
		napi_create_string_utf8(env, "onmessage", strlen("onmessage"), &key) == napi_ok &&
		napi_get_property(env, obj, key, &cb) == napi_ok &&
		napi_typeof(env, cb, &cb_type) == napi_ok &&
		cb_type == napi_function) {
		while (!async.queue.empty()) {
			auto item = async.queue.front();
			napi_value args[2];
			napi_value result;
			if (napi_create_string_utf8(env, item.evt.c_str(), item.evt.size(), args) == napi_ok &&
				napi_create_string_utf8(env, item.data.c_str(), item.data.size(), args + 1) == napi_ok) {
				napi_call_function(env, obj, cb, sizeof(args) / sizeof(napi_value), args, &result);
			}
			async.queue.pop();
		}
	}

	napi_queue_async_work(env, async.work);
}

void init_fb_utils(napi_env env, napi_value exports, napi_value module, void* priv) {
	napi_value fn;
	if (napi_create_function(env, NULL, fb_utils_log, NULL, &fn) == napi_ok) {
		napi_set_named_property(env, exports, "log", fn);
	}
	napi_create_reference(env, exports, 1, &async.obj);
	async.evt = CreateEvent(NULL, TRUE, FALSE, TEXT("async"));
	if (napi_create_async_work(env, execute_callback, complete_callback, NULL, &async.work) == napi_ok) {
		napi_queue_async_work(env, async.work);
	}
}

NAPI_MODULE(fb_utils, init_fb_utils);

void _register_napi_fb_utils() {
	_register_fb_utils();
}
