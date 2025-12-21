#include "engine/fee_model.hpp"

#include <cmath>

namespace helix::engine {

FeeResult FeeModel::compute(const Fill &fill) const {
    FeeResult res;
    res.fee_ccy = cfg_.fee_ccy;
    if (fill.status != FillStatus::Filled || fill.filled_qty <= 0.0 || fill.vwap_price <= 0.0) {
        return res;
    }
    const double notional = fill.vwap_price * fill.filled_qty;
    const double bps = (fill.liquidity == Liquidity::Maker) ? cfg_.maker_bps : cfg_.taker_bps;
    double fee = notional * (bps / 1e4);
    fee = round_fee(fee);
    res.fee = fee;
    res.fee_bps = (notional > 0.0) ? (fee / notional) * 1e4 : 0.0;
    return res;
}

double FeeModel::round_fee(double fee) const {
    if (cfg_.rounding == "ceil_to_cent") {
        return std::ceil(fee * 100.0) / 100.0;
    }
    return fee;
}

}  // namespace helix::engine
