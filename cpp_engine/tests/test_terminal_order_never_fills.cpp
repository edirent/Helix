#include <cassert>
#include <vector>

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
    // Scenario 1: short ttl, expire before trades arrive.
    MakerParams params;
    params.q_init = 0.0;
    params.alpha = 1.0;
    params.expire_ms = 50;
    const double tick = 0.1;
    MakerQueueSim sim(params, tick);

    auto book0 = mk_book(100.0, 100.0, 101.0, 100.0);
    for (int i = 0; i < 100; ++i) {
        Action a{};
        a.side = Side::Buy;
        a.size = 1.0;
        a.limit_price = 100.0;
        a.is_maker = true;
        a.order_id = static_cast<uint64_t>(i + 1);
        sim.submit(a, book0, /*now*/0);
    }
    // Advance past expiry with no trades.
    auto fills = sim.on_book(book0, /*now*/60, {});
    assert(fills.empty());

    // Trades arrive after expiry; should produce no fills.
    TradePrint tp{};
    tp.side = Side::Sell;
    tp.price = 100.0;
    tp.size = 1000.0;
    std::vector<TradePrint> trades{tp};
    fills = sim.on_book(book0, /*now*/100, trades);
    assert(fills.empty());

    // Scenario 2: replace semantics; old order must never fill after cancel/replace.
    MakerQueueSim sim2(params, tick);
    Action old{};
    old.side = Side::Buy;
    old.size = 1.0;
    old.limit_price = 100.0;
    old.is_maker = true;
    old.order_id = 42;
    sim2.submit(old, book0, /*now*/0);

    // Replace after 10ms with a new order id and price.
    sim2.cancel(old.order_id);
    Action newer = old;
    newer.order_id = 43;
    newer.limit_price = 99.9;
    sim2.submit(newer, book0, /*now*/10);

    // Trades hit the old price; only the new order could be eligible.
    TradePrint tp2{};
    tp2.side = Side::Sell;
    tp2.price = 100.0;
    tp2.size = 10.0;
    fills = sim2.on_book(book0, /*now*/20, {tp2});
    for (const auto &f : fills) {
        assert(f.order_id == newer.order_id);  // old must never fill
    }

    return 0;
}
