#include <cassert>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::MatchingEngine matcher;
    helix::engine::OrderbookSnapshot book{100.0, 101.0, 10.0, 12.0};

    helix::engine::Action buy{helix::engine::Side::Buy, 2.0};
    auto fill_buy = matcher.simulate(buy, book);
    assert(fill_buy.price == 101.0);
    assert(fill_buy.qty == 2.0);
    assert(fill_buy.side == helix::engine::Side::Buy);

    helix::engine::Action hold{helix::engine::Side::Hold, 0.0};
    auto fill_hold = matcher.simulate(hold, book);
    assert(fill_hold.qty == 0.0);
    return 0;
}
