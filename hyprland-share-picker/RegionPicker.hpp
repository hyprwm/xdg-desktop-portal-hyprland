#pragma once

#include "PickerData.hpp"

#include <optional>
#include <vector>

std::optional<SSelection> selectRegion(const std::vector<SOutputEntry>& outputs);
