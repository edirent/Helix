#pragma once

#include "engine/types.hpp"

namespace helix::engine {

inline bool is_crossing_limit(const Action &action, const OrderbookSnapshot &book) {
    if (action.limit_price <= 0.0) {
        return false;
    }
    if (action.side == Side::Buy) {
        return book.best_ask > 0.0 && action.limit_price >= book.best_ask;
    }
    if (action.side == Side::Sell) {
        return book.best_bid > 0.0 && action.limit_price <= book.best_bid;
    }
    return false;
}

}  // namespace helix::engine
