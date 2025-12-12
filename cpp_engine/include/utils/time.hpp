#pragma once

#include <chrono>

namespace helix::utils {

inline std::chrono::nanoseconds now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
}

}  // namespace helix::utils
