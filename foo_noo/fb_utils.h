#pragma once

#include "../deps/json-2.1.1/json.hpp"
using json = nlohmann::json;

void fb_utils_emit(const char *evt, std::initializer_list<json> init);

void _register_napi_fb_utils();
