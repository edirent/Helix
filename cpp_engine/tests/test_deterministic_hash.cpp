#include <cassert>
#include <cmath>

#include "engine/deterministic_hash.hpp"
#include "engine/latency.hpp"

using namespace helix::engine;

int main() {
    const std::string key = "SIM#1#42";
    uint64_t h = fnv1a64(key);
    // Known FNV-1a result for this string (platform independent).
    assert(h == 6924961391117258329ULL);

    LatencyConfig cfg;
    cfg.base_ms = 8.0;
    cfg.jitter_ms = 4.0;
    cfg.tail_ms = 12.0;
    cfg.tail_prob = 0.02;
    double lat = deterministic_latency_ms("SIM", 1, 42, cfg);
    // Deterministic given FNV seed; allow tiny fp tolerance.
    assert(std::abs(lat - 8.4710278614420691) < 1e-12);
    return 0;
}
