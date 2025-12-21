#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "engine/deterministic_hash.hpp"

namespace helix::engine {

struct LatencyConfig {
    double base_ms{8.0};
    double jitter_ms{4.0};
    double tail_ms{12.0};
    double tail_prob{0.02};
    std::string source{"default"};
};

inline double deterministic_latency_ms(const std::string &symbol, uint64_t seq, uint64_t action_idx,
                                       const LatencyConfig &cfg) {
    std::string seed_input = symbol + "#" + std::to_string(seq) + "#" + std::to_string(action_idx);
    uint64_t seed_val = fnv1a64(seed_input);
    std::mt19937_64 rng(seed_val);
    std::uniform_real_distribution<double> jitter_dist(0.0, cfg.jitter_ms);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    double lat = cfg.base_ms + jitter_dist(rng);
    if (u01(rng) < cfg.tail_prob) {
        lat += cfg.tail_ms;
    }
    return lat;
}

}  // namespace helix::engine
