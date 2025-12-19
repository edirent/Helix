#include <cassert>
#include <cmath>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

static OrderbookSnapshot make_book() {
    OrderbookSnapshot ob;
    ob.best_bid = 99.0;
    ob.best_ask = 101.0;
    ob.bid_size = 2.0;
    ob.ask_size = 2.0;
    ob.bids = {{99.0, 2.0}, {98.0, 2.0}};
    ob.asks = {{101.0, 1.0}, {102.0, 1.0}};
    return ob;
}

int main() {
    const double tick = 0.1;
    MatchingEngine matcher("SIM", tick, false);
    auto book = make_book();

    Action limit_cross;
    limit_cross.side = Side::Buy;
    limit_cross.size = 1.5;
    limit_cross.limit_price = 102.0;  // crosses best_ask
    limit_cross.is_maker = true;      // should still behave as taker when crossing

    Action market;
    market.side = Side::Buy;
    market.size = 1.5;
    market.is_maker = false;

    auto fill_cross = matcher.simulate(limit_cross, book);
    auto fill_mkt = matcher.simulate(market, book);

    assert(fill_cross.status == FillStatus::Filled);
    assert(fill_mkt.status == FillStatus::Filled);

    assert(std::abs(fill_cross.filled_qty - fill_mkt.filled_qty) < 1e-9);
    assert(std::abs(fill_cross.vwap_price - fill_mkt.vwap_price) < 1e-9);
    assert(fill_cross.levels_crossed == fill_mkt.levels_crossed);
    assert(std::abs(fill_cross.slippage_ticks - fill_mkt.slippage_ticks) < 1e-9);

    // Fee bps equivalence (taker rate 6 bps)
    const double notional = fill_cross.vwap_price * fill_cross.filled_qty;
    const double fee_bps_cross = (notional > 0.0) ? (0.0006 * notional / notional) * 1e4 : 0.0;
    const double fee_bps_mkt = fee_bps_cross;
    assert(std::abs(fee_bps_cross - fee_bps_mkt) < 1e-9);

    // Exec cost ticks relative to mid
    const double mid = (book.best_bid + book.best_ask) / 2.0;
    const double exec_cross = (fill_cross.vwap_price - mid) / tick;
    const double exec_mkt = (fill_mkt.vwap_price - mid) / tick;
    assert(std::abs(exec_cross - exec_mkt) < 1e-9);

    return 0;
}
