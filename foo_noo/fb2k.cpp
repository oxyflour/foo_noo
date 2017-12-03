#include <queue>
#include <thread>

// its wired but we have to include winsock2 sdk before libuv header
#include <winsock2.h>

#include "fb2k.h"
#include "decoder.h"

#include "../node/deps/uv/include/uv.h"
#include "../node/src/node.h"
#include "../node/src/node_api.h"

struct RENDERER_INFO {
	char path[4096] = { 0 };
	double position = 0;
	double length = 0;
};

napi_status json_parse(napi_env env, napi_value str, napi_value *result) {
	napi_value global;
	napi_value JSON;
	napi_value parse;
	napi_get_global(env, &global);
	napi_get_named_property(env, global, "JSON", &JSON);
	napi_get_named_property(env, JSON, "parse", &parse);
	return napi_call_function(env, JSON, parse, 1, &str, result);
}

napi_status console_error(napi_env env, napi_value *exception) {
	napi_value global;
	napi_value console;
	napi_value error;
	napi_value result;
	napi_get_global(env, &global);
	napi_get_named_property(env, global, "console", &console);
	napi_get_named_property(env, console, "error", &error);
	return napi_call_function(env, console, error, 1, exception, &result);
}

class meta_formatter {
public:
	meta_formatter(metadb_handle_ptr &item) {
		item->get_browse_info_merged(fi);
	}
	const std::string format(const char *name) {
		pfc::string8_fastalloc tmp;
		fi.meta_format(name, tmp);
		return std::string(tmp.c_str());
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
		{ "albumArtist", meta->format("album artist") },
		{ "album", meta->format("album") },
		{ "trackNumber", meta->format("trackNumber") },
	});
}

struct DUMPED_LIBRARY {
	std::string jsonText;
};

static class dump_library : public main_thread_callback {
public:
	HANDLE evt;
	DUMPED_LIBRARY *ri;
	virtual void callback_run() {
		try {
			static_api_ptr_t<library_manager> lib;
			pfc::list_t<metadb_handle_ptr> list;
			lib->get_all_items(list);
			auto arr = nlohmann::json::array();
			for (t_size i = 0, n = list.get_count(); i < n; i++) {
				arr.push_back(meta_to_json(list.get_item(i), lib));
			}
			if (ri) {
				ri->jsonText = arr.dump();
			}
		}
		catch (std::exception &e) {
			console::error(e.what());
		}
		SetEvent(evt);
	}
};

static class play_load : public main_thread_callback {
public:
	HANDLE evt;
	char path[4096] = { 0 };
	double subsong = 0;
	virtual void callback_run() {
		try {
			static_api_ptr_t<playback_control> pc;
			pc->stop();

			static_api_ptr_t<playlist_manager> plm;
			plm->queue_flush();

			pfc::list_t<metadb_handle_ptr> temp;
			static_api_ptr_t<playlist_incoming_item_filter> pliif;
			pliif->process_location(path, temp, false, NULL, NULL, NULL);

			auto len = temp.get_count();
			for (auto i = 0; i < len; i++) {
				auto item = temp.get_item(i);
				if (item->get_subsong_index() == subsong) {
					plm->queue_add_item(item);
				}
			}

			pc->set_stop_after_current(true);
		}
		catch (std::exception &e) {
			console::error(e.what());
		}
		SetEvent(evt);
	}
};

static class play_query : public main_thread_callback {
public:
	HANDLE evt;
	RENDERER_INFO *ri;
	virtual void callback_run() {
		try {
			static_api_ptr_t<playback_control> pc;
			metadb_handle_ptr media;
			if (ri && pc->get_now_playing(media)) {
				strcpy(ri->path, media->get_path());
				ri->position = pc->playback_get_position();
				ri->length = pc->playback_get_length();
			}
		}
		catch (std::exception &e) {
			console::error(e.what());
		}
		SetEvent(evt);
	}
};

static class play_start : public main_thread_callback {
public:
	HANDLE evt;
	virtual void callback_run() {
		try {
			static_api_ptr_t<playback_control> pc;
			pc->play_or_unpause();
		}
		catch (std::exception &e) {
			console::error(e.what());
		}
		SetEvent(evt);
	}
};

static class play_pause : public main_thread_callback {
public:
	HANDLE evt;
	virtual void callback_run() {
		try {
			static_api_ptr_t<playback_control> pc;
			pc->pause(true);
		}
		catch (std::exception &e) {
			console::error(e.what());
		}
		SetEvent(evt);
	}
};

static class play_seek : public main_thread_callback {
public:
	HANDLE evt;
	double time = 0;
	virtual void callback_run() {
		try {
			static_api_ptr_t<playback_control> pc;
			pc->playback_seek(time);
		}
		catch (std::exception &e) {
			console::error(e.what());
		}
		SetEvent(evt);
	}
};

const int MAX_LOG_BUFFER = 1024 * 16;

napi_value fb2k_log(napi_env env, napi_callback_info info) {
	napi_value args[16];
	size_t argc = sizeof(args) / sizeof(napi_value);
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

	size_t offset = 0;
	char buffer[MAX_LOG_BUFFER] = { 0 };
	for (auto i = 0; i < argc && offset < sizeof(buffer); i++) {
		if (offset) {
			buffer[offset++] = ' ';
		}
		napi_value arg;
		size_t len = 0;
		if (napi_coerce_to_string(env, args[i], &arg) == napi_ok &&
			napi_get_value_string_utf8(env, arg, buffer + offset, sizeof(buffer) - offset, &len) == napi_ok) {
			offset += len;
		}
	}
	console::print(buffer);

	return nullptr;
}

const int SEND_TIMEOUT = 30000;

napi_value fb2k_send(napi_env env, napi_callback_info info) {
	napi_value args[16];
	size_t argc = sizeof(args) / sizeof(napi_value);
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

	char buffer[4096] = { 0 };
	t_size len;
	t_size recvd = 0;
	napi_value arg;
	for (int i = 0; i < argc &&
			recvd + 1 < sizeof(buffer) &&
			napi_coerce_to_string(env, args[i], &arg) == napi_ok &&
			napi_get_value_string_utf8(env, arg, buffer + recvd, sizeof(buffer) - recvd, &len) == napi_ok;
			i ++) {
		recvd += len + 1;
	}

	static_api_ptr_t<main_thread_callback_manager> cbm;
	if (strcmp(buffer, "library:dump") == 0) {
		auto cb = new service_impl_t<dump_library>();
		DUMPED_LIBRARY ri;
		cb->ri = &ri;
		auto evt = cb->evt = CreateEvent(NULL, FALSE, FALSE, NULL);
		cbm->add_callback(cb);
		napi_value result = nullptr;
		if (WaitForSingleObject(evt, SEND_TIMEOUT) == WAIT_OBJECT_0) {
			napi_value jsonString;
			napi_create_string_utf8(env, ri.jsonText.c_str(), ri.jsonText.size(), &jsonString);
			json_parse(env, jsonString, &result);
		}
		CloseHandle(evt);
		return result;
	}
	else if (strcmp(buffer, "renderer:load") == 0) {
		auto cb = new service_impl_t<play_load>();
		auto evt = cb->evt = CreateEvent(NULL, FALSE, FALSE, NULL);
		auto path = buffer + strlen(buffer) + 1;
		strcpy(cb->path, path);
		auto subsong = path + strlen(path) + 1;
		cb->subsong = atoi(subsong);
		cbm->add_callback(cb);
		WaitForSingleObject(evt, SEND_TIMEOUT);
		CloseHandle(evt);
	}
	else if (strcmp(buffer, "renderer:play") == 0) {
		auto cb = new service_impl_t<play_start>();
		auto evt = cb->evt = CreateEvent(NULL, FALSE, FALSE, NULL);
		cbm->add_callback(cb);
		WaitForSingleObject(evt, SEND_TIMEOUT);
		CloseHandle(evt);
	}
	else if (strcmp(buffer, "renderer:pause") == 0) {
		auto cb = new service_impl_t<play_pause>();
		auto evt = cb->evt = CreateEvent(NULL, FALSE, FALSE, NULL);
		cbm->add_callback(cb);
		WaitForSingleObject(evt, SEND_TIMEOUT);
		CloseHandle(evt);
	}
	else if (strcmp(buffer, "renderer:seek") == 0) {
		auto cb = new service_impl_t<play_seek>();
		auto evt = cb->evt = CreateEvent(NULL, FALSE, FALSE, NULL);
		cb->time = atof(buffer + strlen(buffer) + 1);
		cbm->add_callback(cb);
		WaitForSingleObject(evt, SEND_TIMEOUT);
		CloseHandle(evt);
	}
	else if (strcmp(buffer, "renderer:query") == 0) {
		auto cb = new service_impl_t<play_query>();
		auto evt = cb->evt = CreateEvent(NULL, FALSE, FALSE, NULL);
		RENDERER_INFO ri;
		cb->ri = &ri;
		cbm->add_callback(cb);
		napi_value result = nullptr;
		if (WaitForSingleObject(evt, SEND_TIMEOUT) == WAIT_OBJECT_0) {
			napi_create_object(env, &result);

			napi_value path;
			napi_create_string_utf8(env, ri.path, strlen(ri.path), &path);
			napi_set_named_property(env, result, "path", path);

			napi_value position;
			napi_create_double(env, ri.position, &position);
			napi_set_named_property(env, result, "position", position);

			napi_value length;
			napi_create_double(env, ri.length, &length);
			napi_set_named_property(env, result, "length", length);
		}
		CloseHandle(evt);
		return result;
	}

	return nullptr;
}

napi_value fb2k_get_albumart(napi_env env, napi_callback_info info) {
	napi_value args[2];
	size_t argc = sizeof(args) / sizeof(napi_value);
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

	char path[1024] = { 0 };
	size_t len = sizeof(path);
	napi_get_value_string_utf8(env, args[0], path, len, &len);

	int subsong = 0;
	napi_get_value_int32(env, args[1], &subsong);

	abort_callback_dummy cb;
	auto media_list = pfc::list_t<metadb_handle_ptr>();
	media_list.add_item(static_api_ptr_t<metadb>()->handle_create(path, subsong));
	auto album_list = pfc::list_t<GUID>();
	album_list.add_item(album_art_ids::cover_front);

	try {
		auto extractor = static_api_ptr_t<album_art_manager_v2>()->open(media_list, album_list, cb);
		auto albumart = extractor->query(album_art_ids::cover_front, cb);
		auto ptr = albumart->get_ptr();
		napi_value buffer;
		napi_create_buffer_copy(env, albumart->get_size(), ptr, (void **)&ptr, &buffer);
		return buffer;
	}
	catch (std::exception &e) {
		console::printf("Error: get albumart for %s failed: %s", path, e.what());
		return nullptr;
	}
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

void check_exception(napi_env env) {
	auto hasException = false;
	napi_value exception;
	if (napi_is_exception_pending(env, &hasException) == napi_ok &&
		hasException &&
		napi_get_and_clear_last_exception(env, &exception) == napi_ok) {
		console_error(env, &exception);
	}
}

void complete_callback(napi_env env, napi_status status, void* data) {
	napi_value obj;
	napi_value key;
	napi_value cb;
	napi_valuetype cbType;
	napi_get_reference_value(env, async.obj, &obj);
	napi_create_string_utf8(env, "emit", strlen("emit"), &key);
	napi_get_property(env, obj, key, &cb);
	napi_typeof(env, cb, &cbType);

	if (cbType == napi_function) {
		while (!async.queue.empty()) {
			auto item = async.queue.front();
			napi_value callArgs[2];
			napi_value jsonString;
			napi_value result;
			size_t callArgc = sizeof(callArgs) / sizeof(napi_value);
			if (napi_create_string_utf8(env, item.evt.c_str(), item.evt.size(), callArgs) == napi_ok &&
				napi_create_string_utf8(env, item.data.c_str(), item.data.size(), &jsonString) == napi_ok &&
				json_parse(env, jsonString, callArgs + 1) == napi_ok &&
				napi_call_function(env, obj, cb, callArgc, callArgs, &result) == napi_ok) {
				// message is successfully handled
			}
			else {
				check_exception(env);
			}
			async.queue.pop();
		}
	}

	napi_queue_async_work(env, async.work);
}

void export_func(napi_env env, napi_value exports, char *name, napi_callback cb) {
	napi_value fn;
	napi_create_function(env, NULL, NAPI_AUTO_LENGTH, cb, NULL, &fn);
	napi_set_named_property(env, exports, name, fn);
}

napi_value init_fb2k(napi_env env, napi_value exports) {
	export_func(env, exports, "log", fb2k_log);
	export_func(env, exports, "send", fb2k_send);
	export_func(env, exports, "getAlbumart", fb2k_get_albumart);

	napi_value dll_path;
	auto path = core_api::get_my_full_path();
	napi_create_string_utf8(env, path, strlen(path), &dll_path);
	napi_set_named_property(env, exports, "dllPath", dll_path);

	async.evt = CreateEvent(NULL, FALSE, FALSE, TEXT("async"));
	napi_create_reference(env, exports, 1, &async.obj);
	napi_value resource_name;
	napi_create_string_utf8(env, "fb2kevts", NAPI_AUTO_LENGTH, &resource_name);
	napi_create_async_work(env, nullptr, resource_name,
		execute_callback, complete_callback, NULL, &async.work);
	napi_queue_async_work(env, async.work);

	napi_value cons;
	decoder::register_constructor(env, "Decoder", &cons);
	napi_set_named_property(env, exports, "Decoder", cons);

	return exports;
}

NAPI_MODULE(fb2k, init_fb2k);

void _register_napi_fb2k() {
	_register_fb2k();
}
