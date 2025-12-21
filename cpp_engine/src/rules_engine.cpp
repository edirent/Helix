#include "engine/rules_engine.hpp"

#include <cmath>

namespace helix::engine {
namespace {

bool near_multiple(double value, double step, double &normalized) {
    if (step <= 0.0) {
        normalized = value;
        return true;
    }
    const double steps = std::round(value / step);
    normalized = steps * step;
    return true;
}

double ref_price_for_action(const Action &action, const OrderbookSnapshot &book) {
    if (action.limit_price > 0.0) {
        return action.limit_price;
    }
    if (action.side == Side::Buy) {
        return (book.best_ask > 0.0) ? book.best_ask : book.best_bid;
    }
    if (action.side == Side::Sell) {
        return (book.best_bid > 0.0) ? book.best_bid : book.best_ask;
    }
    return 0.0;
}

}  // namespace

RulesResult RulesEngine::apply(const Action &action, const OrderbookSnapshot &book) const {
    RulesResult res;
    res.normalized = action;

    if (action.side != Side::Buy && action.side != Side::Sell) {
        res.reason = RejectReason::BadSide;
        return res;
    }

    if (action.size <= 0.0) {
        res.reason = RejectReason::ZeroQty;
        return res;
    }

    double norm_qty = action.size;
    if (cfg_.qty_step > 0.0) {
        near_multiple(action.size, cfg_.qty_step, norm_qty);
    }
    if (norm_qty < cfg_.min_qty - 1e-9) {
        res.reason = RejectReason::MinQty;
        return res;
    }

    double norm_price = action.limit_price;
    if (action.limit_price > 0.0 && cfg_.tick_size > 0.0) {
        near_multiple(action.limit_price, cfg_.tick_size, norm_price);
    } else if (action.is_maker && action.limit_price <= 0.0) {
        norm_price = (action.side == Side::Buy) ? book.best_bid : book.best_ask;
    }

    res.normalized.size = norm_qty;
    res.normalized.limit_price = norm_price;

    const double ref_price = ref_price_for_action(res.normalized, book);
    if (!(ref_price > 0.0)) {
        res.reason = RejectReason::PriceInvalid;
        return res;
    }

    const double notional = norm_qty * ref_price;
    if (cfg_.min_notional > 0.0 && notional < cfg_.min_notional - 1e-9) {
        res.reason = RejectReason::MinNotional;
        return res;
    }

    res.ok = true;
    res.reason = RejectReason::None;
    return res;
}

}  // namespace helix::engine
