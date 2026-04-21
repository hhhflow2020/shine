#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>
#include <absl/time/time.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace shine {

namespace asio = boost::asio;
using asio::awaitable;
using asio::use_awaitable;
using asio::ip::tcp;

using Status   = absl::Status;
template <typename T>
using StatusOr = absl::StatusOr<T>;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

} // namespace shine
