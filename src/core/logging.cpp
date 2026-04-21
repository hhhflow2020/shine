#include "core/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <absl/strings/ascii.h>

namespace shine::log {

void init(std::string_view level_sv) {
    static bool initialized = false;
    if (!initialized) {
        auto logger = spdlog::stdout_color_mt("shine");
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e %^%l%$ [%t] %v");
        initialized = true;
    }

    std::string s(level_sv);
    absl::AsciiStrToLower(&s);
    auto lvl = spdlog::level::info;
    if (s == "trace")      lvl = spdlog::level::trace;
    else if (s == "debug") lvl = spdlog::level::debug;
    else if (s == "info")  lvl = spdlog::level::info;
    else if (s == "warn" || s == "warning") lvl = spdlog::level::warn;
    else if (s == "error") lvl = spdlog::level::err;
    else if (s == "off")   lvl = spdlog::level::off;
    spdlog::set_level(lvl);
}

} // namespace shine::log
