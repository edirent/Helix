#include <cassert>

#include "engine/fee_model.hpp"
#include "engine/rules_engine.hpp"

int main() {
    helix::engine::RulesConfig cfg{};
    cfg.tick_size = 0.1;
    cfg.qty_step = 0.001;
    cfg.min_qty = 0.001;
    cfg.min_notional = 5.0;
    helix::engine::RulesEngine rules(cfg);

    helix::engine::OrderbookSnapshot book{};
    book.best_bid = 99.0;
    book.best_ask = 100.0;

    helix::engine::Action a;
    a.side = helix::engine::Side::Buy;
    a.size = 0.0005;
    auto r = rules.apply(a, book);
    assert(!r.ok && r.reason == helix::engine::RejectReason::MinQty);

    a.size = 0.0013;
    r = rules.apply(a, book);
    assert(!r.ok && r.reason == helix::engine::RejectReason::MinNotional);

    a.size = 0.01;
    r = rules.apply(a, book);
    assert(!r.ok && r.reason == helix::engine::RejectReason::MinNotional);

    a.size = 0.1;
    a.limit_price = 100.04;
    r = rules.apply(a, book);
    assert(r.ok);
    assert(r.normalized.size == 0.1);
    assert(r.normalized.limit_price == 100.0);

    helix::engine::FeeConfig fcfg{2.0, 6.0, "USDT", "ceil_to_cent", "test"};
    helix::engine::FeeModel fee_model(fcfg);
    helix::engine::Fill fill;
    fill.status = helix::engine::FillStatus::Filled;
    fill.liquidity = helix::engine::Liquidity::Maker;
    fill.side = helix::engine::Side::Buy;
    fill.vwap_price = 100.0;
    fill.filled_qty = 1.0;
    auto fres = fee_model.compute(fill);
    assert(fres.fee == 0.02);
    assert(fres.fee_bps > 1.99 && fres.fee_bps < 2.01);

    fill.liquidity = helix::engine::Liquidity::Taker;
    fres = fee_model.compute(fill);
    assert(fres.fee == 0.06);
    assert(fres.fee_bps > 5.99 && fres.fee_bps < 6.01);

    return 0;
}
