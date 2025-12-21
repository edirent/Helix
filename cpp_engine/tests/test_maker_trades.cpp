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
    OrderbookSnapshot book = mk_book(100.0, 5.0, 100.5, 5.0);
    MakerParams params;
    params.q_init = 0.0;
    params.alpha = 0.5;
    params.expire_ms = 1000;
    MakerQueueSim sim(params, 0.1);

    Action maker_buy{};
    maker_buy.side = Side::Buy;
    maker_buy.limit_price = 100.0;
    maker_buy.size = 1.0;
    maker_buy.is_maker = true;
    sim.submit(maker_buy, book, 0);

    // No trades, no depth change -> no fill.
    std::vector<TradePrint> trades;
    auto fills = sim.on_book(book, 10, trades);
    assert(fills.empty());

    // Trades hitting bid should consume queue then fill.
    trades.push_back(TradePrint{15, Side::Sell, 100.0, 1.0, "t1"});
    fills = sim.on_book(book, 20, trades);
    assert(!fills.empty());
    assert(fills.front().liquidity == Liquidity::Maker);
    assert(fills.front().filled_qty > 0.0);

    // Perturb params: higher q_init/alpha still yields a fill from trades (robustness)
    MakerParams params2;
    params2.q_init = 0.9;
    params2.alpha = 0.7;
    params2.expire_ms = 1000;
    MakerQueueSim sim2(params2, 0.1);
    sim2.submit(maker_buy, book, 0);
    std::vector<TradePrint> trades2 = {TradePrint{25, Side::Sell, 100.0, 10.0, "t2"}};
    fills = sim2.on_book(book, 30, trades2);
    assert(!fills.empty());
    return 0;
}
