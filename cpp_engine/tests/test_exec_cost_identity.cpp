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
    ob.bids = {{99.0, 2.0}};
    ob.asks = {{101.0, 1.0}, {102.0, 1.0}};
    return ob;
}

static void check_identity(const Fill &fill, const OrderbookSnapshot &book, double tick) {
    const double mid = (book.best_bid + book.best_ask) / 2.0;
    const double best = (fill.side == Side::Buy) ? book.best_ask : book.best_bid;
    const double exec_cost = (fill.side == Side::Buy) ? (fill.vwap_price - mid) / tick : (mid - fill.vwap_price) / tick;
    const double slip = (fill.side == Side::Buy) ? (fill.vwap_price - best) / tick : (best - fill.vwap_price) / tick;
    const double mid_to_best = (fill.side == Side::Buy) ? (best - mid) / tick : (mid - best) / tick;
    const double lhs = exec_cost;
    const double rhs = slip + mid_to_best;
    assert(std::abs(lhs - rhs) < 1e-9);
}

int main() {
    const double tick = 0.1;
    MatchingEngine matcher("SIM", tick, false);
    auto book = make_book();

    // BUY
    Action buy{};
    buy.side = Side::Buy;
    buy.size = 1.5;
    auto fill_buy = matcher.simulate(buy, book);
    assert(fill_buy.status == FillStatus::Filled);
    check_identity(fill_buy, book, tick);

    // SELL
    Action sell{};
    sell.side = Side::Sell;
    sell.size = 1.5;
    auto fill_sell = matcher.simulate(sell, book);
    assert(fill_sell.status == FillStatus::Filled);
    check_identity(fill_sell, book, tick);

    return 0;
}
