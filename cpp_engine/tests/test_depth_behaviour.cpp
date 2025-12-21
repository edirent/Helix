#include <cassert>
#include <cmath>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

static OrderbookSnapshot make_book(double best_bid, double bid_qty, double best_ask, double ask_qty) {
    OrderbookSnapshot ob;
    ob.best_bid = best_bid;
    ob.bid_size = bid_qty;
    ob.best_ask = best_ask;
    ob.ask_size = ask_qty;
    if (best_bid > 0 && bid_qty > 0) {
        ob.bids.push_back({best_bid, bid_qty});
    }
    if (best_ask > 0 && ask_qty > 0) {
        ob.asks.push_back({best_ask, ask_qty});
    }
    return ob;
}

int main() {
    const double tick = 0.1;

    // IOC (default) should partially fill when depth is insufficient.
    {
        MatchingEngine matcher("SIM", tick, /*reject_on_insufficient_depth=*/false);
        OrderbookSnapshot book = make_book(99.0, 0.5, 101.0, 0.5);
        Action buy;
        buy.side = Side::Buy;
        buy.size = 2.0;
        auto fill = matcher.simulate(buy, book);
        assert(fill.status == FillStatus::Filled);
        assert(std::abs(fill.filled_qty - 0.5) < 1e-9);
        assert(std::abs(fill.unfilled_qty - 1.5) < 1e-9);
        assert(fill.partial);
    }

    // FOK (reject_on_insufficient_depth) should reject when depth is insufficient.
    {
        MatchingEngine matcher("SIM", tick, /*reject_on_insufficient_depth=*/true);
        OrderbookSnapshot book = make_book(99.0, 0.5, 101.0, 0.5);
        Action buy;
        buy.side = Side::Buy;
        buy.size = 2.0;
        auto fill = matcher.simulate(buy, book);
        assert(fill.status == FillStatus::Rejected);
        assert(fill.reason == RejectReason::NoLiquidity);
        assert(fill.filled_qty == 0.0);
    }

    // Empty ask side should reject BUY.
    {
        MatchingEngine matcher("SIM", tick, false);
        OrderbookSnapshot book = make_book(99.0, 1.0, 0.0, 0.0);
        Action buy;
        buy.side = Side::Buy;
        buy.size = 1.0;
        auto fill = matcher.simulate(buy, book);
        assert(fill.status == FillStatus::Rejected);
        assert(fill.reason == RejectReason::NoAsk);
    }

    // Empty bid side should reject SELL.
    {
        MatchingEngine matcher("SIM", tick, false);
        OrderbookSnapshot book = make_book(0.0, 0.0, 101.0, 1.0);
        Action sell;
        sell.side = Side::Sell;
        sell.size = 1.0;
        auto fill = matcher.simulate(sell, book);
        assert(fill.status == FillStatus::Rejected);
        assert(fill.reason == RejectReason::NoBid);
    }

    // Very small qty should still fill (no negative/zero surprises).
    {
        MatchingEngine matcher("SIM", tick, false);
        OrderbookSnapshot book = make_book(99.0, 1.0, 101.0, 1.0);
        const double tiny = 1e-9;
        Action buy;
        buy.side = Side::Buy;
        buy.size = tiny;
        auto fill = matcher.simulate(buy, book);
        assert(fill.status == FillStatus::Filled);
        assert(std::abs(fill.filled_qty - tiny) < 1e-12);
    }

    return 0;
}
