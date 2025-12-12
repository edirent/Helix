#include "engine/feature_engine.hpp"

#include <algorithm>
#include <cmath>

namespace helix::engine {

Feature FeatureEngine::compute(const OrderbookSnapshot &book, const TradeTape &tape) const {
    Feature f;
    const double spread = std::max(0.0, book.best_ask - book.best_bid);
    const double mid = spread > 0 ? book.best_bid + spread / 2.0 : book.best_bid;
    const double depth = book.bid_size + book.ask_size;

    f.imbalance = depth > 0 ? (book.bid_size - book.ask_size) / depth : 0.0;
    f.microprice = depth > 0 ? (book.best_ask * book.bid_size + book.best_bid * book.ask_size) / depth : mid;
    f.pressure_bid = book.bid_size;
    f.pressure_ask = book.ask_size;
    f.sweep_signal = (spread > 0) ? tape.last_size / (depth + 1e-6) : 0.0;
    f.trend_strength = (spread > 0) ? (tape.last_price - mid) / (spread + 1e-6) : 0.0;
    return f;
}

}  // namespace helix::engine
