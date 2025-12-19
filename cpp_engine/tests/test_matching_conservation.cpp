#include <cassert>
#include <cmath>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::MatchingEngine matcher("SIM", 0.1);
    helix::engine::OrderbookSnapshot book;
    book.asks = {{101.0, 1.0}, {102.0, 1.0}, {103.0, 1.0}};
    book.bids = {{99.0, 1.0}, {98.0, 1.0}, {97.0, 1.0}};
    book.best_ask = 101.0;
    book.ask_size = 1.0;
    book.best_bid = 99.0;
    book.bid_size = 1.0;

    helix::engine::Action buy{helix::engine::Side::Buy, 2.5};
    auto fill = matcher.simulate(buy, book);

    assert(fill.status == helix::engine::FillStatus::Filled);
    assert(std::abs(fill.filled_qty - 2.5) < 1e-9);
    assert(fill.unfilled_qty == 0.0);
    assert(fill.levels_crossed == 3);

    const double expected_vwap = (101.0 * 1.0 + 102.0 * 1.0 + 103.0 * 0.5) / 2.5;
    assert(std::abs(fill.vwap_price - expected_vwap) < 1e-9);

    const double expected_slip_ticks = (expected_vwap - book.best_ask) / 0.1;
    assert(std::abs(fill.slippage_ticks - expected_slip_ticks) < 1e-9);
    return 0;
}
