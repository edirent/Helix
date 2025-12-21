#include <cassert>

#include "engine/rules_engine.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::RulesConfig cfg{};
    cfg.tick_size = 0.1;
    cfg.qty_step = 0.01;
    cfg.min_qty = 0.01;
    cfg.min_notional = 0.5;
    helix::engine::RulesEngine rules(cfg);
    helix::engine::OrderbookSnapshot book{};
    book.best_bid = 99.95;
    book.best_ask = 100.05;

    helix::engine::Action a;
    a.side = helix::engine::Side::Buy;
    a.size = 0.013;            // off-step
    a.limit_price = 100.04;    // off-step
    auto res = rules.apply(a, book);
    assert(res.ok);
    // qty rounded to nearest step
    assert(res.normalized.size == 0.01);
    // price rounded to nearest tick
    assert(res.normalized.limit_price == 100.0);

    helix::engine::Action small;
    small.side = helix::engine::Side::Buy;
    small.size = 0.0001;  // rounds to zero step
    small.limit_price = 100.0;
    auto res2 = rules.apply(small, book);
    assert(!res2.ok);
    assert(res2.reason == helix::engine::RejectReason::MinQty);

    helix::engine::Action notionalFail;
    notionalFail.side = helix::engine::Side::Buy;
    notionalFail.size = 0.01;
    notionalFail.limit_price = 0.1;
    auto res3 = rules.apply(notionalFail, book);
    assert(!res3.ok);
    assert(res3.reason == helix::engine::RejectReason::MinNotional);
    return 0;
}
