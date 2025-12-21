#include <cassert>

#include "engine/rules_engine.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::RulesConfig cfg{};
    cfg.tick_size = 0.1;
    cfg.qty_step = 0.01;
    cfg.min_qty = 0.001;
    cfg.min_notional = 0.0;
    helix::engine::RulesEngine rules(cfg);

    helix::engine::OrderbookSnapshot book{};
    book.best_bid = 100.0;
    book.best_ask = 100.2;

    helix::engine::Action buy{};
    buy.side = helix::engine::Side::Buy;
    buy.limit_price = 100.19;  // should floor to 100.1, not round up to crossing
    buy.size = 1.019;          // should floor to 1.01
    auto r = rules.apply(buy, book);
    assert(r.ok);
    assert(r.normalized.limit_price == 100.1);
    assert(r.normalized.size == 1.01);

    helix::engine::Action sell{};
    sell.side = helix::engine::Side::Sell;
    sell.limit_price = 100.01;  // should ceil to 100.1 (away from bid)
    sell.size = 2.237;
    r = rules.apply(sell, book);
    assert(r.ok);
    assert(r.normalized.limit_price == 100.1);
    assert(r.normalized.size == 2.23);

    return 0;
}
