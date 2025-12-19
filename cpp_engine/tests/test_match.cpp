#include <cassert>
#include <cmath>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::MatchingEngine matcher("SIM", 0.5);
    helix::engine::OrderbookSnapshot book;
    book.asks = {{101.0, 1.0}, {101.5, 2.0}};
    book.bids = {{100.0, 1.5}, {99.5, 2.5}};
    book.best_ask = book.asks.front().price;
    book.ask_size = book.asks.front().qty;
    book.best_bid = book.bids.front().price;
    book.bid_size = book.bids.front().qty;

    helix::engine::Action buy{helix::engine::Side::Buy, 2.5};
    auto fill_buy = matcher.simulate(buy, book);
    assert(fill_buy.status == helix::engine::FillStatus::Filled);
    assert(std::abs(fill_buy.vwap_price - 101.3) < 1e-6);
    assert(std::abs(fill_buy.filled_qty - 2.5) < 1e-6);
    assert(fill_buy.levels_crossed == 2);
    assert(!fill_buy.partial);
    assert(fill_buy.unfilled_qty == 0.0);
    assert(fill_buy.slippage_ticks > 0.0);

    helix::engine::Action sell{helix::engine::Side::Sell, 5.0};
    auto fill_sell = matcher.simulate(sell, book);
    assert(fill_sell.status == helix::engine::FillStatus::Filled);
    assert(fill_sell.partial);
    assert(fill_sell.unfilled_qty > 0.0);
    assert(fill_sell.levels_crossed == 2);
    assert(fill_sell.filled_qty == 4.0);  // 1.5 + 2.5
    return 0;
}
