#pragma once

#include <spdlog/spdlog.h>
#include <string_view>

namespace shine::log {

void init(std::string_view level);

inline auto& logger() { return *spdlog::default_logger(); }

} // namespace shine::log

#define SHINE_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define SHINE_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define SHINE_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define SHINE_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define SHINE_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
