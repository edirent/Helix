#include "engine/recorder.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace helix::engine {

Recorder::Recorder(const std::string &path) : out_(path, std::ios::app) {}

Recorder::~Recorder() { flush(); }

void Recorder::record(const Event &event) {
    if (!out_) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_MSC_VER)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%F %T");
    out_ << ss.str() << " | " << static_cast<int>(event.type) << " | " << event.payload << '\n';
}

void Recorder::flush() { out_.flush(); }

}  // namespace helix::engine
