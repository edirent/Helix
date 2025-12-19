#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <vector>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

struct GeneratedBook {
    OrderbookSnapshot book;
};

static GeneratedBook gen_book(std::mt19937 &rng, double tick_size) {
    std::uniform_int_distribution<int> depth_dist(1, 5);
    std::uniform_real_distribution<double> qty_dist(0.01, 5.0);
    std::uniform_real_distribution<double> price_dist(90.0, 110.0);
    int asks_n = depth_dist(rng);
    int bids_n = depth_dist(rng);

    std::vector<PriceLevel> asks, bids;
    for (int i = 0; i < asks_n; ++i) {
        asks.push_back({price_dist(rng) + i * tick_size, qty_dist(rng)});
    }
    for (int i = 0; i < bids_n; ++i) {
        bids.push_back({price_dist(rng) - i * tick_size, qty_dist(rng)});
    }
    std::sort(asks.begin(), asks.end(), [](const auto &a, const auto &b) { return a.price < b.price; });
    std::sort(bids.begin(), bids.end(), [](const auto &a, const auto &b) { return a.price > b.price; });

    OrderbookSnapshot ob;
    ob.asks = asks;
    ob.bids = bids;
    ob.best_ask = asks.empty() ? 0.0 : asks.front().price;
    ob.best_bid = bids.empty() ? 0.0 : bids.front().price;
    ob.ask_size = asks.empty() ? 0.0 : asks.front().qty;
    ob.bid_size = bids.empty() ? 0.0 : bids.front().qty;
    return {ob};
}

static void check_fill(const Fill &fill, const Action &action, const OrderbookSnapshot &book, double tick) {
    if (fill.status != FillStatus::Filled) {
        return;
    }
    assert(fill.filled_qty >= 0.0);
    assert(fill.filled_qty <= action.size + 1e-9);
    assert(fill.unfilled_qty >= -1e-9);

    const auto &levels = (action.side == Side::Buy) ? book.asks : book.bids;
    double remaining = action.size;
    double consumed = 0.0;
    double notional = 0.0;
    std::size_t crossed = 0;
    for (const auto &lvl : levels) {
        if (remaining <= 0.0) break;
        if (lvl.qty <= 0.0) continue;
        const double traded = std::min(remaining, lvl.qty);
        remaining -= traded;
        consumed += traded;
        notional += traded * lvl.price;
        if (traded > 0.0) {
            ++crossed;
        }
    }
    assert(std::abs(consumed - fill.filled_qty) < 1e-6);
    if (fill.filled_qty > 0.0) {
        const double vwap = notional / fill.filled_qty;
        assert(std::abs(vwap - fill.vwap_price) < 1e-6);
        const double best = (action.side == Side::Buy)
                                ? (book.asks.empty() ? book.best_ask : book.asks.front().price)
                                : (book.bids.empty() ? book.best_bid : book.bids.front().price);
        if (best > 0.0 && tick > 0.0) {
            const double slip_ticks = (action.side == Side::Buy) ? (vwap - best) / tick : (best - vwap) / tick;
            assert(std::abs(slip_ticks - fill.slippage_ticks) < 1e-6);
        }
    }
    assert(fill.levels_crossed == crossed);
    if (fill.unfilled_qty > 0.0) {
        assert(fill.partial);
    }
}

int main() {
    std::vector<double> ticks = {0.01, 0.1, 1.0};
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> qty_dist(0.01, 10.0);
    std::uniform_int_distribution<int> side_dist(0, 1);

    for (double tick : ticks) {
        MatchingEngine matcher_ioc("SIM", tick, false);
        MatchingEngine matcher_fok("SIM", tick, true);
        for (int iter = 0; iter < 200; ++iter) {
            auto gb = gen_book(rng, tick);
            auto book = gb.book;
            if (book.best_ask <= 0.0 || book.best_bid <= 0.0) continue;
            Action action;
            action.side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            action.size = qty_dist(rng);
            action.is_maker = false;

            // IOC path
            auto fill = matcher_ioc.simulate(action, book);
            if (fill.status == FillStatus::Filled) {
                check_fill(fill, action, book, tick);
            }

            // FOK path: if not enough depth, should reject.
            auto fill_fok = matcher_fok.simulate(action, book);
            double total_depth = 0.0;
            const auto &levels = (action.side == Side::Buy) ? book.asks : book.bids;
            for (const auto &lvl : levels) total_depth += lvl.qty;
            if (total_depth + 1e-9 < action.size) {
                assert(fill_fok.status == FillStatus::Rejected);
            } else if (fill_fok.status == FillStatus::Filled) {
                check_fill(fill_fok, action, book, tick);
                assert(!fill_fok.partial);
            }
        }
    }

    return 0;
}
