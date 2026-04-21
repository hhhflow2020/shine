#pragma once

#include "config/schema.hpp"
#include "core/common.hpp"

#include <string>

namespace shine::config {

StatusOr<Config> loadFromFile(const std::string& path);
StatusOr<Config> loadFromString(const std::string& content);

} // namespace shine::config
