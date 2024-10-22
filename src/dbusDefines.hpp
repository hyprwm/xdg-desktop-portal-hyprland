#pragma once

#include <sdbus-c++/sdbus-c++.h>

typedef sdbus::Struct<uint32_t, std::unordered_map<std::string, sdbus::Variant>> dbUasv;