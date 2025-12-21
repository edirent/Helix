#include <cassert>
#include <cmath>
#include <vector>

#include "engine/types.hpp"

struct Pending {
    double mid_at_fill{0.0};
    helix::engine::Side side{helix::engine::Side::Hold};
    int64_t target_ts_ms{0};
};

int main() {
    const double tick_size = 0.1;
    std::vector<Pending> pending;
    pending.push_back(Pending{100.0, helix::engine::Side::Buy, 100});

    auto process = [&](int64_t ts_ms, double mid, std::vector<Pending> &pend, std::vector<double> &out) {
        std::vector<Pending> remaining;
        for (const auto &p : pend) {
            if (ts_ms >= p.target_ts_ms && mid > 0.0) {
                const double delta = mid - p.mid_at_fill;
                const double adv = (p.side == helix::engine::Side::Buy ? delta : -delta) / tick_size;
                out.push_back(adv);
            } else {
                remaining.push_back(p);
            }
        }
        pend.swap(remaining);
    };

    std::vector<double> advs;
    process(50, 101.0, pending, advs);
    assert(advs.empty());
    assert(pending.size() == 1);

    process(100, 99.0, pending, advs);
    assert(advs.size() == 1);
    assert(std::abs(advs[0] + 10.0) < 1e-9);
    assert(pending.empty());
    return 0;
}
