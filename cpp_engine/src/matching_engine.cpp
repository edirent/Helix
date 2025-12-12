#include "engine/matching_engine.hpp"

namespace helix::engine {

Fill MatchingEngine::simulate(const Action &action, const OrderbookSnapshot &book) const {
    Fill fill;
    fill.side = action.side;
    fill.qty = action.size;
    fill.partial = false;

    if (action.side == Side::Buy) {
        fill.price = book.best_ask > 0 ? book.best_ask : book.best_bid;
    } else if (action.side == Side::Sell) {
        fill.price = book.best_bid > 0 ? book.best_bid : book.best_ask;
    } else {
        fill.price = 0.0;
        fill.qty = 0.0;
    }

    return fill;
}

}  // namespace helix::engine
