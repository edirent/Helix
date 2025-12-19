#include <cassert>
#include <cmath>

#include "engine/feature_engine.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::FeatureEngine fe;
    helix::engine::OrderbookSnapshot book{0, 100.0, 101.0, 5.0, 3.0};
    helix::engine::TradeTape tape{100.5, 1.0};

    auto feature = fe.compute(book, tape);
    const double expected_imbalance = (5.0 - 3.0) / (5.0 + 3.0);
    assert(std::abs(feature.imbalance - expected_imbalance) < 1e-6);
    assert(feature.microprice > 0.0);
    return 0;
}
