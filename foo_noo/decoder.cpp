#include "decoder.h"
#include <assert.h>

struct WAVE_FORMAT_HEADER {
	// RIFF WAVE CHUNK
	char szRiffID[4];		// 'R','I','F','F'
	DWORD dwRiffSize;		// file size - 8
	char szRiffFormat[4];	// 'W','A','V','E'

	// FORMAT CHUNK
	char szFmtID[4];		// 'f', 'm', 't', ' '
	DWORD dwFormatSize;		// 16 or 18 (for wExtra)
	WORD wFormatTag;		// 0x001
	WORD wChannels;			// 1 or 2
	DWORD dwSamplesPerSec;
	DWORD dwAvgBytesPerSec;
	WORD wBlockAlign;
	WORD wBitsPerSample;
	//	WORD wExtra;

	// DATA CHUNK
	char szDataID[4];		// 'd','a','t','a'
	DWORD dwDataSize;		// size of data
};

static WAVE_FORMAT_HEADER create_wave_header(WORD channels, DWORD srate, DWORD dataSize, WORD wavbit) {
	WAVE_FORMAT_HEADER wf;

	memcpy_s(wf.szRiffID, 4, "RIFF", 4);
	wf.dwRiffSize = dataSize + sizeof(WAVE_FORMAT_HEADER) - 8;
	memcpy_s(wf.szRiffFormat, 4, "WAVE", 4);

	memcpy_s(wf.szFmtID, 4, "fmt ", 4);
	wf.dwFormatSize = 16;
	wf.wFormatTag = 0x001;
	wf.wChannels = channels;
	wf.dwSamplesPerSec = srate;
	wf.dwAvgBytesPerSec = wf.dwSamplesPerSec * wavbit / 8 * wf.wChannels;
	wf.wBlockAlign = wavbit / 8 * wf.wChannels;
	wf.wBitsPerSample = wavbit;
	//	wf.wExtra = 0;

	memcpy_s(wf.szDataID, 4, "data", 4);
	wf.dwDataSize = dataSize;
	return wf;
}

napi_ref decoder::cons_ref;

napi_value decoder::New(napi_env env, napi_callback_info info) {
	auto obj = new decoder();
	napi_value self;
	napi_value args[3];
	size_t argc = sizeof(args) / sizeof(napi_value);
	size_t len;
	napi_get_cb_info(env, info, &argc, args, &self, nullptr);
	napi_get_value_string_utf8(env, args[0], obj->file_path, sizeof(obj->file_path), &len);
	if (argc > 1) {
		napi_get_value_int32(env, args[1], &obj->subsong_index);
	}
	if (argc > 2) {
		napi_get_value_int32(env, args[2], &obj->wave_bit);
	}
	napi_wrap(env, self, obj, Destroy, nullptr, &obj->this_ref);

	abort_callback_dummy cb;
	obj->input.open_path(NULL, obj->file_path, cb, false, false);
	obj->input.open_decoding(obj->subsong_index, 0, cb);
	obj->input.run(obj->chunk, cb);
	obj->has_first_chunk = true;

	file_info_impl fi;
	obj->input.get_info(obj->subsong_index, fi, cb);
	obj->data_length = (int)(fi.get_length() *
		obj->chunk.get_sample_rate() * obj->chunk.get_channel_count() * obj->wave_bit / 8);

	return self;
}

void decoder::Destroy(napi_env env, void *ptr, void *hint) {
	auto obj = (decoder *)ptr;
	if (!obj->finished) {
		obj->finished = true;
		obj->input.close();
		napi_delete_reference(env, obj->this_ref);
	}
}

napi_value decoder::length(napi_env env, napi_callback_info info) {
	napi_value self;
	napi_get_cb_info(env, info, nullptr, nullptr, &self, nullptr);
	decoder *obj;
	napi_unwrap(env, self, (void **)&obj);

	napi_value length;
	napi_create_int32(env, sizeof(WAVE_FORMAT_HEADER) + obj->data_length, &length);
	return length;
}

napi_value decoder::header(napi_env env, napi_callback_info info) {
	napi_value self;
	napi_get_cb_info(env, info, nullptr, nullptr, &self, nullptr);
	decoder *obj;
	napi_unwrap(env, self, (void **)&obj);

	if (obj->finished) {
		return nullptr;
	}

	auto data_length = obj->data_length;
	if (data_length > 0) {
		auto wave_header = create_wave_header(obj->chunk.get_channel_count(), obj->chunk.get_sample_rate(), data_length, obj->wave_bit);
		auto ptr = &wave_header;

		napi_value buffer;
		napi_create_buffer_copy(env, sizeof(wave_header), &wave_header, (void **)&ptr, &buffer);
		return buffer;
	}
	else {
		return nullptr;
	}
}

napi_value decoder::decode(napi_env env, napi_callback_info info) {
	napi_value self;
	napi_get_cb_info(env, info, nullptr, nullptr, &self, nullptr);
	decoder *obj;
	napi_unwrap(env, self, (void **)&obj);

	if (obj->finished) {
		return nullptr;
	}

	abort_callback_dummy cb;
	if (obj->has_first_chunk || obj->input.run(obj->chunk, cb)) {
		obj->has_first_chunk = false;

		mem_block_container_impl_t<> buf;
		static_api_ptr_t<audio_postprocessor> proc;
		proc->run(obj->chunk, buf, obj->wave_bit, obj->wave_bit, false, 1.0);

		napi_value buffer;
		auto ptr = buf.get_ptr();
		napi_create_buffer_copy(env, buf.get_size(), ptr, &ptr, &buffer);
		return buffer;
	}
	else {
		return nullptr;
	}
}

napi_value decoder::destroy(napi_env env, napi_callback_info info) {
	napi_value self;
	napi_get_cb_info(env, info, nullptr, nullptr, &self, nullptr);
	decoder *obj;
	napi_unwrap(env, self, (void **)&obj);

	Destroy(env, obj, NULL);
	return nullptr;
}
