#pragma once

#include <cstdint>
#include <string>

namespace helix::engine {

inline uint64_t fnv1a64(const std::string &s) {
    uint64_t hash = 1469598103934665603ULL;
    constexpr uint64_t prime = 1099511628211ULL;
    for (unsigned char c : s) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }
    return hash;
}

}  // namespace helix::engine
