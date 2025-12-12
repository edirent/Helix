#include "engine/risk_engine.hpp"

#include <cmath>

namespace helix::engine {

bool RiskEngine::validate(const Action &action, double last_price) const {
    double projected_qty = position_.qty;
    if (action.side == Side::Buy) {
        projected_qty += action.size;
    } else if (action.side == Side::Sell) {
        projected_qty -= action.size;
    }

    const double projected_notional = std::abs(projected_qty) * std::abs(last_price);
    return std::abs(projected_qty) <= max_position_ && projected_notional <= max_notional_;
}

void RiskEngine::update(const Fill &fill) {
    const double signed_qty = (fill.side == Side::Buy ? fill.qty : -fill.qty);
    const double previous_qty = position_.qty;
    const double new_qty = previous_qty + signed_qty;

    if (new_qty == 0.0) {
        position_.pnl += (fill.price - position_.avg_price) * previous_qty;
        position_.avg_price = 0.0;
    } else {
        const double gross_value = position_.avg_price * previous_qty + fill.price * signed_qty;
        position_.avg_price = gross_value / new_qty;
    }
    position_.qty = new_qty;
}

}  // namespace helix::engine
