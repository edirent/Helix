#include <cassert>
#include <queue>

#include "engine/matching_engine.hpp"
#include "engine/types.hpp"

struct PendingAction {
    helix::engine::Action action;
    int64_t fill_ts{0};
};

struct PendingCmp {
    bool operator()(const PendingAction &a, const PendingAction &b) const { return a.fill_ts > b.fill_ts; }
};

int main() {
    const double tick_size = 0.1;
    helix::engine::MatchingEngine matcher("SIM", tick_size);

    helix::engine::OrderbookSnapshot book_t0;
    book_t0.ts_ms = 0;
    book_t0.best_ask = 101.0;
    book_t0.ask_size = 10.0;
    book_t0.best_bid = 99.0;
    book_t0.bid_size = 10.0;
    book_t0.asks = {{101.0, 10.0}};
    book_t0.bids = {{99.0, 10.0}};

    helix::engine::OrderbookSnapshot book_t5;
    book_t5.ts_ms = 5;
    book_t5.best_ask = 111.0;
    book_t5.ask_size = 10.0;
    book_t5.best_bid = 109.0;
    book_t5.bid_size = 10.0;
    book_t5.asks = {{111.0, 10.0}};
    book_t5.bids = {{109.0, 10.0}};

    helix::engine::OrderbookSnapshot book_t11 = book_t5;
    book_t11.ts_ms = 11;  // simulate time reaching decision_ts + latency

    const double target_notional = 1000.0;
    helix::engine::Action demo_action;
    demo_action.side = helix::engine::Side::Buy;
    demo_action.notional = target_notional;
    demo_action.size = target_notional / book_t0.best_ask;  // sized off decision-time best ask (101)

    std::priority_queue<PendingAction, std::vector<PendingAction>, PendingCmp> pending;
    pending.push(PendingAction{demo_action, /*fill_ts=*/11});

    auto process_book = [&](const helix::engine::OrderbookSnapshot &book, helix::engine::Fill *out_fill) {
        const int64_t now = book.ts_ms;
        while (!pending.empty() && pending.top().fill_ts <= now) {
            PendingAction pa = pending.top();
            pending.pop();
            *out_fill = matcher.simulate(pa.action, book);
        }
    };

    helix::engine::Fill fill{};
    process_book(book_t0, &fill);
    assert(fill.status != helix::engine::FillStatus::Filled);  // latency not reached

    process_book(book_t5, &fill);
    assert(fill.status != helix::engine::FillStatus::Filled);  // still before fill_ts

    process_book(book_t11, &fill);
    assert(fill.status == helix::engine::FillStatus::Filled);
    assert(fill.liquidity == helix::engine::Liquidity::Taker);
    assert(std::abs(fill.vwap_price - 111.0) < 1e-9);  // should use post-latency best ask
    assert(fill.levels_crossed == 1);

    const double mid = (book_t11.best_bid + book_t11.best_ask) / 2.0;  // 110
    const double exec_cost_ticks = (fill.vwap_price - mid) / tick_size;
    assert(exec_cost_ticks > 0.0);  // buy should pay above mid
    assert(std::abs(exec_cost_ticks - 10.0) < 1e-9);
    return 0;
}
