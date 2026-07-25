/*
 * Copyright (c) 2026 Gabriel2392
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

#include <fmt/format.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace spdlog {

namespace level {
enum level_enum { trace, debug, info, warn, err, critical, off };
} // namespace level

namespace detail {

inline std::atomic<level::level_enum>& current_level() {
  static std::atomic<level::level_enum> value(level::debug);
  return value;
}

inline std::mutex& callback_mutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::function<void(level::level_enum, const std::string&)>& callback() {
  static std::function<void(level::level_enum, const std::string&)> sink;
  return sink;
}

inline const char* tag() { return "BrokkrNative"; }

inline int android_priority(level::level_enum level_value) {
#if defined(__ANDROID__)
  switch (level_value) {
    case level::trace:
    case level::debug:
      return ANDROID_LOG_DEBUG;
    case level::info:
      return ANDROID_LOG_INFO;
    case level::warn:
      return ANDROID_LOG_WARN;
    case level::err:
    case level::critical:
      return ANDROID_LOG_ERROR;
    case level::off:
      return ANDROID_LOG_SILENT;
  }
#endif
  return 0;
}

inline const char* level_name(level::level_enum level_value) {
  switch (level_value) {
    case level::trace:
      return "trace";
    case level::debug:
      return "debug";
    case level::info:
      return "info";
    case level::warn:
      return "warn";
    case level::err:
      return "error";
    case level::critical:
      return "critical";
    case level::off:
      return "off";
  }
  return "unknown";
}

inline void emit(level::level_enum level_value, const std::string& message) {
  if (level_value < current_level().load(std::memory_order_relaxed) || level_value == level::off) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(callback_mutex());
    if (callback()) {
      callback()(level_value, message);
    }
  }

#if defined(__ANDROID__)
  __android_log_write(android_priority(level_value), tag(), message.c_str());
#else
  std::fprintf(stderr, "[%s] %s\n", level_name(level_value), message.c_str());
#endif
}

template <typename... Args>
inline void log(level::level_enum level_value, fmt::format_string<Args...> pattern, Args&&... args) {
  if (level_value < current_level().load(std::memory_order_relaxed) || level_value == level::off) {
    return;
  }
  emit(level_value, fmt::format(pattern, std::forward<Args>(args)...));
}

} // namespace detail

inline void set_level(level::level_enum level_value) { detail::current_level().store(level_value, std::memory_order_relaxed); }

inline bool should_log(level::level_enum level_value) {
  return level_value >= detail::current_level().load(std::memory_order_relaxed) && level_value != level::off;
}

inline void set_log_callback(std::function<void(level::level_enum, const std::string&)> callback) {
  std::lock_guard<std::mutex> lock(detail::callback_mutex());
  detail::callback() = std::move(callback);
}

template <typename... Args>
inline void trace(fmt::format_string<Args...> pattern, Args&&... args) {
  detail::log(level::trace, pattern, std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(fmt::format_string<Args...> pattern, Args&&... args) {
  detail::log(level::debug, pattern, std::forward<Args>(args)...);
}

template <typename... Args>
inline void info(fmt::format_string<Args...> pattern, Args&&... args) {
  detail::log(level::info, pattern, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(fmt::format_string<Args...> pattern, Args&&... args) {
  detail::log(level::warn, pattern, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(fmt::format_string<Args...> pattern, Args&&... args) {
  detail::log(level::err, pattern, std::forward<Args>(args)...);
}

template <typename... Args>
inline void critical(fmt::format_string<Args...> pattern, Args&&... args) {
  detail::log(level::critical, pattern, std::forward<Args>(args)...);
}

} // namespace spdlog