#pragma once

#include "../deps/json-2.1.1/json.hpp"
#include "../foobar/foobar2000/SDK/foobar2000.h"

using json = nlohmann::json;

void fb2k_emit(const char *evt, std::initializer_list<json> init);
nlohmann::json meta_to_json(metadb_handle_ptr &item, static_api_ptr_t<library_manager> &lib);

void _register_napi_fb2k();
