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
    const double previous_avg = position_.avg_price;
    const double new_qty = previous_qty + signed_qty;

    // Realized PnL when reducing or flipping.
    if (previous_qty != 0.0 && ((previous_qty > 0 && signed_qty < 0) || (previous_qty < 0 && signed_qty > 0))) {
        const double closed_qty = std::min(std::abs(previous_qty), std::abs(signed_qty));
        const double realized = closed_qty * (fill.price - previous_avg) * (previous_qty > 0 ? 1.0 : -1.0);
        position_.realized_pnl += realized;
        position_.pnl += realized;
    }

    if (new_qty == 0.0) {
        position_.avg_price = 0.0;
    } else {
        const double gross_value = previous_avg * previous_qty + fill.price * signed_qty;
        position_.avg_price = gross_value / new_qty;
    }
    position_.qty = new_qty;
}

}  // namespace helix::engine
