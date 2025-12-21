#include <cassert>
#include <cmath>

#include "engine/fee_model.hpp"
#include "engine/types.hpp"

int main() {
    helix::engine::FeeConfig cfg{2.0, 6.0, "USDT", "none", "test"};
    helix::engine::FeeModel model(cfg);

    helix::engine::Fill fill;
    fill.status = helix::engine::FillStatus::Filled;
    fill.vwap_price = 10.0;
    fill.filled_qty = 5.0;

    fill.liquidity = helix::engine::Liquidity::Maker;
    auto maker_res = model.compute(fill);
    assert(std::abs(maker_res.fee_bps - 2.0) < 1e-9);

    fill.liquidity = helix::engine::Liquidity::Taker;
    auto taker_res = model.compute(fill);
    assert(std::abs(taker_res.fee_bps - 6.0) < 1e-9);
    return 0;
}
