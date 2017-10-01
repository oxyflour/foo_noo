#include <queue>
#include <thread>

// its wired but we have to include winsock2 sdk before libuv header
#include <winsock2.h>

#include "fb2k.h"
#include "../node/deps/uv/include/uv.h"
#include "../node/src/node_api.h"

class wav_decoder {
	napi_value New(napi_env env, napi_callback_info info) {
		napi_targe();
	}
};

class meta_formatter {
public:
	meta_formatter(metadb_handle_ptr &item) {
		item->get_browse_info_merged(fi);
	}
	const char *format(const char *name) {
		pfc::string8_fastalloc tmp;
		fi.meta_format(name, tmp);
		return tmp.c_str();
	}
private:
	file_info_impl fi;
};

nlohmann::json meta_to_json(metadb_handle_ptr &item, static_api_ptr_t<library_manager> &lib) {
	auto meta = new meta_formatter(item);
	pfc::string8 relative_path;
	lib->get_relative_path(item, relative_path);
	return nlohmann::json({
		{ "path", relative_path.c_str() },
		{ "filePath", item->get_path() },
		{ "subsong", item->get_subsong_index() },
		{ "length", item->get_length() },
		{ "time", item->get_filetimestamp() },
		{ "title", meta->format("title") },
		{ "artist", meta->format("artist") },
		{ "albumArtist", meta->format("albumArtist") },
		{ "album", meta->format("album") },
		{ "trackNumber", meta->format("trackNumber") },
	});
}

class main_thread_control : public main_thread_callback {
public:
	std::string evt;
	virtual void callback_run() {
		const char *dump = "library:dump";
		if (evt.substr(0, strlen(dump)).compare(dump) == 0) {
			static_api_ptr_t<library_manager> lib;
			pfc::list_t<metadb_handle_ptr> list;
			lib->get_all_items(list);
			auto arr = nlohmann::json::array();
			for (t_size i = 0, n = list.get_count(); i < n; i++) {
				arr.push_back(meta_to_json(list.get_item(i), lib));
			}
			fb2k_emit(evt.c_str(), {
				{ "type", dump },
				{ "list", arr },
			});
		}
	}
};

const int MAX_LOG_BUFFER = 1024 * 16;

napi_value fb_utils_log(napi_env env, napi_callback_info info) {
	napi_value args[16];
	size_t argc = sizeof(args) / sizeof(napi_value);
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) == napi_ok) {
		size_t offset = 0;
		char buffer[MAX_LOG_BUFFER] = { 0 };
		for (auto i = 0; i < argc; i++) {
			napi_value arg;
			size_t len = 0;
			char str[4096] = { 0 };
			if (napi_coerce_to_string(env, args[i], &arg) == napi_ok &&
				napi_get_value_string_utf8(env, arg, str, sizeof(str), &len) == napi_ok &&
				offset < sizeof(buffer)) {
				offset += sprintf(buffer + offset, offset == 0 ? "%s" : " %s", str);
			}
		}
		console::print(buffer);
	}
	return nullptr;
}

static int send_id = 1;
napi_value fb_utils_send(napi_env env, napi_callback_info info) {
	napi_value args[16];
	size_t argc = sizeof(args) / sizeof(napi_value);
	napi_value ret;
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) == napi_ok) {
		char buffer[1024] = { 0 };
		t_size len;
		t_size recvd = 0;
		for (int i = 0; i < argc &&
				recvd + 1 < sizeof(buffer) &&
				napi_get_value_string_utf8(env, args[i], buffer + recvd, sizeof(buffer) - recvd, &len) == napi_ok; i ++) {
			recvd += len + 1;
		}
		if (strcmp(buffer, "library:dump") == 0) {
			auto cb = new service_impl_t<main_thread_control>();
			auto evt = cb->evt = std::string(buffer) + ":" + std::to_string(send_id++);
			static_api_ptr_t<main_thread_callback_manager> cbm;
			cbm->add_callback(cb);
			napi_create_string_utf8(env, evt.c_str(), evt.size(), &ret);
			return ret;
		}
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

void fb2k_emit(const char *evt, std::initializer_list<json> init) {
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
	napi_value global;
	napi_value JSON;
	napi_value parse;

	napi_value obj;
	napi_value key;
	napi_value cb;
	napi_valuetype cbType;
	if (napi_get_global(env, &global) == napi_ok &&
		napi_get_named_property(env, global, "JSON", &JSON) == napi_ok &&
		napi_get_named_property(env, JSON, "parse", &parse) == napi_ok &&
		napi_get_reference_value(env, async.obj, &obj) == napi_ok &&
		napi_create_string_utf8(env, "_onmessage", strlen("_onmessage"), &key) == napi_ok &&
		napi_get_property(env, obj, key, &cb) == napi_ok &&
		napi_typeof(env, cb, &cbType) == napi_ok &&
		cbType == napi_function) {
		while (!async.queue.empty()) {
			auto item = async.queue.front();
			napi_value callArgs[2];
			napi_value parseArgs[1];
			napi_value result;
			if (napi_create_string_utf8(env, item.evt.c_str(), item.evt.size(), callArgs) == napi_ok &&
				napi_create_string_utf8(env, item.data.c_str(), item.data.size(), parseArgs) == napi_ok &&
				napi_call_function(env, JSON, parse, 1, parseArgs, callArgs + 1) == napi_ok) {
				napi_call_function(env, obj, cb, sizeof(callArgs) / sizeof(napi_value), callArgs, &result);
			}
			async.queue.pop();
		}
	}

	napi_queue_async_work(env, async.work);
}

void export_func(napi_env env, napi_value exports, char *name, napi_callback cb) {
	napi_value fn;
	if (napi_create_function(env, NULL, cb, NULL, &fn) == napi_ok) {
		napi_set_named_property(env, exports, name, fn);
	}
}

void init_fb_utils(napi_env env, napi_value exports, napi_value module, void* priv) {
	export_func(env, exports, "log", fb_utils_log);
	export_func(env, exports, "send", fb_utils_send);

	napi_value dll_path;
	auto path = core_api::get_my_full_path();
	if (napi_create_string_utf8(env, path, strlen(path), &dll_path) == napi_ok) {
		napi_set_named_property(env, exports, "dllPath", dll_path);
	}

	napi_create_reference(env, exports, 1, &async.obj);
	async.evt = CreateEvent(NULL, TRUE, FALSE, TEXT("async"));
	if (napi_create_async_work(env, execute_callback, complete_callback, NULL, &async.work) == napi_ok) {
		napi_queue_async_work(env, async.work);
	}
}

NAPI_MODULE(fb_utils, init_fb_utils);

void _register_napi_fb2k() {
	_register_fb_utils();
}
