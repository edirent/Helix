#include "engine/matching_engine.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace helix::engine {
namespace {

const std::vector<PriceLevel> &side_levels(const OrderbookSnapshot &book, Side side, std::vector<PriceLevel> &fallback) {
    const auto &levels = (side == Side::Buy) ? book.asks : book.bids;
    if (!levels.empty()) {
        return levels;
    }

    // Fallback to top-of-book if depth vectors are empty.
    if (side == Side::Buy && book.best_ask > 0.0 && book.ask_size > 0.0) {
        fallback.push_back(PriceLevel{book.best_ask, book.ask_size});
    } else if (side == Side::Sell && book.best_bid > 0.0 && book.bid_size > 0.0) {
        fallback.push_back(PriceLevel{book.best_bid, book.bid_size});
    }
    return fallback;
}

double best_price_for_side(const OrderbookSnapshot &book, Side side) {
    if (side == Side::Buy) {
        if (!book.asks.empty()) {
            return book.asks.front().price;
        }
        return book.best_ask;
    } else if (side == Side::Sell) {
        if (!book.bids.empty()) {
            return book.bids.front().price;
        }
        return book.best_bid;
    }
    return 0.0;
}

}  // namespace

Fill MatchingEngine::simulate(const Action &action, const OrderbookSnapshot &book) const {
    Fill fill;
    fill.side = action.side;
    fill.status = FillStatus::Rejected;

    if (action.side != Side::Buy && action.side != Side::Sell) {
        fill.reason = RejectReason::BadSide;
        return fill;
    }
    if (action.size <= 0.0) {
        fill.reason = RejectReason::ZeroQty;
        return fill;
    }

    std::vector<PriceLevel> fallback_levels;
    const auto &levels = side_levels(book, action.side, fallback_levels);
    if (levels.empty()) {
        fill.reason = (action.side == Side::Buy) ? RejectReason::NoAsk : RejectReason::NoBid;
        return fill;
    }

    double remaining = action.size;
    double filled = 0.0;
    double notional = 0.0;
    std::size_t levels_crossed = 0;
    double consumed_sum = 0.0;

    for (const auto &lvl : levels) {
        if (remaining <= 0.0) {
            break;
        }
        if (lvl.qty <= 0.0) {
            continue;
        }
        const double traded = std::min(remaining, lvl.qty);
        remaining -= traded;
        filled += traded;
        notional += traded * lvl.price;
        consumed_sum += traded;
        ++levels_crossed;
    }

    if (filled <= 0.0) {
        fill.reason = RejectReason::NoLiquidity;
        return fill;
    }

    if (reject_on_insufficient_depth_ && remaining > 0.0) {
        return Fill::rejected(action.side, RejectReason::NoLiquidity);
    }

    const double vwap = notional / filled;
    const double best_price = best_price_for_side(book, action.side);
    double slippage_ticks = 0.0;
    if (best_price > 0.0 && tick_size_ > 0.0) {
        if (action.side == Side::Buy) {
            slippage_ticks = (vwap - best_price) / tick_size_;
        } else {
            slippage_ticks = (best_price - vwap) / tick_size_;
        }
    }

    fill.status = FillStatus::Filled;
    fill.reason = RejectReason::None;
    fill.partial = remaining > 0.0;
    fill.qty = filled;
    fill.filled_qty = filled;
    fill.unfilled_qty = remaining > 0.0 ? remaining : 0.0;
    fill.price = vwap;
    fill.vwap_price = vwap;
    fill.levels_crossed = levels_crossed;
    fill.slippage_ticks = slippage_ticks;
    fill.liquidity = Liquidity::Taker;

#ifndef NDEBUG
    const double eps = 1e-9;
    assert(filled <= action.size + eps);
    assert(std::abs(filled - consumed_sum) < eps);
    assert(levels_crossed == fill.levels_crossed);
    if (filled > 0.0) {
        assert(std::abs(vwap - notional / filled) < eps);
        if (best_price > 0.0 && tick_size_ > 0.0) {
            const double expected_slip = (action.side == Side::Buy) ? (vwap - best_price) : (best_price - vwap);
            assert(std::abs(fill.slippage_ticks - expected_slip / tick_size_) < 1e-6);
        }
    }
#endif
    return fill;
}

}  // namespace helix::engine
