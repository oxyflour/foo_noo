#include "fb_env.h"
#include "../foobar/foobar2000/SDK/foobar2000.h"
#include "../node/src/node_api.h"

void init_fb_env(napi_env env, napi_value exports, napi_value module, void* priv) {
	napi_value dll_path;
	auto path = core_api::get_my_full_path();
	if (napi_create_string_utf8(env, path, strlen(path), &dll_path) == napi_ok) {
		napi_set_named_property(env, exports, "dllPath", dll_path);
	}
}

NAPI_MODULE(fb_env, init_fb_env);

void _register_napi_fb_env() {
	_register_fb_env();
}