#pragma once

#include "../foobar/foobar2000/SDK/foobar2000.h"
#include "../foobar/foobar2000/helpers/input_helpers.h"

#include "../node/src/node_api.h"

class decoder {
	napi_ref this_ref;
	bool finished = false;
	input_helper input;

	bool has_first_chunk = false;
	audio_chunk_impl_temporary chunk;
	int data_length;

	char file_path[1024] = { 0 };
	int subsong_index = 0;
	int wave_bit = 16;
public:
	static napi_status register_constructor(napi_env env, const char *name, napi_value *cons);
	static napi_value New(napi_env env, napi_callback_info info);
	static void Destroy(napi_env env, void *ptr, void *hint);
	static napi_value length(napi_env env, napi_callback_info info);
	static napi_value header(napi_env env, napi_callback_info info);
	static napi_value decode(napi_env env, napi_callback_info info);
	static napi_value destroy(napi_env env, napi_callback_info info);
};
