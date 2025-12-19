#include <cassert>

#include "engine/order_utils.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

static OrderbookSnapshot make_book(double bid, double ask) {
    OrderbookSnapshot ob;
    ob.best_bid = bid;
    ob.best_ask = ask;
    if (bid > 0) ob.bids.push_back({bid, 1.0});
    if (ask > 0) ob.asks.push_back({ask, 1.0});
    return ob;
}

int main() {
    auto book = make_book(99.0, 101.0);

    Action buy;
    buy.side = Side::Buy;
    buy.limit_price = 100.0;
    assert(!is_crossing_limit(buy, book));
    buy.limit_price = 101.0;
    assert(is_crossing_limit(buy, book));

    Action sell;
    sell.side = Side::Sell;
    sell.limit_price = 100.0;
    assert(!is_crossing_limit(sell, book));
    sell.limit_price = 99.0;
    assert(is_crossing_limit(sell, book));

    return 0;
}
