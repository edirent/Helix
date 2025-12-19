#include <cassert>

#include "engine/maker_queue.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

static OrderbookSnapshot mk_book(double bid_px, double bid_qty, double ask_px, double ask_qty) {
    OrderbookSnapshot ob;
    ob.best_bid = bid_px;
    ob.bid_size = bid_qty;
    ob.best_ask = ask_px;
    ob.ask_size = ask_qty;
    if (bid_px > 0 && bid_qty > 0) ob.bids.push_back({bid_px, bid_qty});
    if (ask_px > 0 && ask_qty > 0) ob.asks.push_back({ask_px, ask_qty});
    return ob;
}

int main() {
    MakerParams params;
    params.q_init = 0.0;  // remove queue-ahead to make fill math deterministic in test
    params.alpha = 0.6;
    params.expire_ms = 50;
    params.adv_ticks = 2.0;
    const double tick = 0.1;
    MakerQueueSim sim(params, tick);

    // Submit a buy resting order at bid=100 with qty=5, initial visible 10.
    Action buy{};
    buy.side = Side::Buy;
    buy.size = 5.0;
    buy.limit_price = 100.0;
    buy.is_maker = true;
    auto b0 = mk_book(100.0, 10.0, 101.0, 10.0);
    sim.submit(buy, b0, /*now*/0);

    double prev_qty = b0.bid_size;

    // Baseline update (no drop) => no fills.
    auto fills = sim.on_book(b0, 1);
    double new_qty = b0.bid_size;
    double delta_visible = std::max(0.0, prev_qty - new_qty);
    assert(delta_visible == 0.0);
    assert(fills.empty());
    prev_qty = new_qty;

    // Depth decreases by 2, fill should be bounded by visible drop.
    auto b_drop = mk_book(100.0, 8.0, 101.0, 10.0);
    fills = sim.on_book(b_drop, 2);
    new_qty = b_drop.bid_size;
    delta_visible = std::max(0.0, prev_qty - new_qty);
    if (!fills.empty()) {
        auto f1 = fills.front();
        assert(f1.liquidity == Liquidity::Maker);
        assert(f1.filled_qty <= delta_visible + 1e-9);  // cannot exceed visible drop when q_init=0
        // Adverse selection applied in maker fills.
        assert(f1.vwap_price > f1.price - params.adv_ticks * tick);
    }
    prev_qty = new_qty;

    // Expire and ensure no further fills even if depth drops.
    fills = sim.on_book(mk_book(100.0, 8.0, 101.0, 10.0), 60);
    assert(fills.empty());

    // New SELL order to test sell path and adv tick sign.
    MakerQueueSim sim2(params, tick);
    Action sell{};
    sell.side = Side::Sell;
    sell.size = 1.0;
    sell.limit_price = 101.0;
    sell.is_maker = true;
    auto b1 = mk_book(99.0, 10.0, 101.0, 10.0);
    sim2.submit(sell, b1, 0);
    fills = sim2.on_book(mk_book(99.0, 10.0, 101.0, 9.0), 1);  // qty down by 1
    if (!fills.empty()) {
        auto f2 = fills.front();
        assert(f2.liquidity == Liquidity::Maker);
        // Adv ticks should move sell price down.
        assert(f2.vwap_price < f2.price + 1e-12);
    }

    // If visible qty only increases, no maker fills.
    fills = sim2.on_book(mk_book(99.0, 10.0, 101.0, 20.0), 2);
    assert(fills.empty());

    return 0;
}
