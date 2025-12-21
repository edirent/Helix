#include <cassert>
#include <cmath>

#include "engine/fee_model.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::FeeConfig cfg{2.0, 6.0, "USDT", "none", "test"};
    helix::engine::FeeModel model(cfg);

    helix::engine::Fill maker_fill;
    maker_fill.status = helix::engine::FillStatus::Filled;
    maker_fill.liquidity = helix::engine::Liquidity::Maker;
    maker_fill.vwap_price = 10.0;
    maker_fill.filled_qty = 1.23;
    auto res = model.compute(maker_fill);
    const double expected_fee_raw = 10.0 * 1.23 * (2.0 / 1e4);
    const double expected_fee = expected_fee_raw;
    assert(std::abs(res.fee - expected_fee) < 1e-9);
    assert(std::abs(res.fee_bps - 2.0) < 1e-6);

    helix::engine::Fill taker_fill = maker_fill;
    taker_fill.liquidity = helix::engine::Liquidity::Taker;
    auto res2 = model.compute(taker_fill);
    const double expected_fee_raw_taker = 10.0 * 1.23 * (6.0 / 1e4);
    const double expected_fee_taker = expected_fee_raw_taker;
    assert(std::abs(res2.fee - expected_fee_taker) < 1e-9);
    assert(std::abs(res2.fee_bps - 6.0) < 1e-6);
    return 0;
}
