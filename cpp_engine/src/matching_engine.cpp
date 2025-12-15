#include "engine/matching_engine.hpp"

namespace helix::engine {

Fill MatchingEngine::simulate(const Action &action, const OrderbookSnapshot &book) const {
    Fill fill;
    fill.side = action.side;
    fill.qty = action.size;
    fill.partial = false;

    if (action.side == Side::Buy) {
      if (book.best_ask <= 0) return Fill::reject(action, "no_ask");
      fill.price = book.best_ask;
    } else if (action.side == Side::Sell) {
      if (book.best_bid <= 0) return Fill::reject(action, "no_bid");
      fill.price = book.best_bid;
    } else {
      return Fill::reject(action, "bad_side");
    }

    return fill;
}

}  // namespace helix::engine
