/**
 * Lightweight logger used across the engine. Intended to be replaced with a faster sink later.
 */
#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace helix::utils {

enum class LogLevel { Debug, Info, Warn, Error };

inline std::string level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "LOG";
    }
}

inline void log(LogLevel level, const std::string &message) {
    using clock = std::chrono::system_clock;
    const auto now = clock::to_time_t(clock::now());
    std::tm tm_buf{};
#if defined(_MSC_VER)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%F %T");
    std::cerr << "[" << ss.str() << "][" << level_to_string(level) << "] " << message << std::endl;
}

inline void debug(const std::string &msg) { log(LogLevel::Debug, msg); }
inline void info(const std::string &msg) { log(LogLevel::Info, msg); }
inline void warn(const std::string &msg) { log(LogLevel::Warn, msg); }
inline void error(const std::string &msg) { log(LogLevel::Error, msg); }

}  // namespace helix::utils
