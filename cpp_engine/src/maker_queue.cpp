#include "engine/maker_queue.hpp"

#include <algorithm>
#include <cmath>

namespace helix::engine {

MakerQueueSim::MakerQueueSim(MakerParams params, double tick_size) : params_(params), tick_size_(tick_size) {}

void MakerQueueSim::submit(const engine::Action &action, const engine::OrderbookSnapshot &book, int64_t now_ts) {
    RestingOrder ord;
    ord.action = action;
    ord.order_id = action.order_id;
    ord.price = (action.limit_price > 0.0) ? action.limit_price
                                           : (action.side == engine::Side::Buy ? book.best_bid : book.best_ask);
    ord.my_qty = action.size;
    ord.submit_ts = now_ts;
    ord.expire_ts = now_ts + params_.expire_ms;
    ord.queue_ahead = level_qty(book, ord.price, action.side) * params_.q_init;
    orders_.push_back(ord);
}

bool MakerQueueSim::cancel(uint64_t order_id) {
    auto it = std::remove_if(orders_.begin(), orders_.end(),
                             [&](const RestingOrder &o) { return o.order_id == order_id; });
    if (it == orders_.end()) {
        return false;
    }
    orders_.erase(it, orders_.end());
    return true;
}

std::vector<engine::Fill> MakerQueueSim::on_book(const engine::OrderbookSnapshot &book, int64_t now_ts,
                                                 const std::vector<engine::TradePrint> &trades) {
    std::vector<engine::Fill> fills;
    update_level_maps(book);

    std::vector<RestingOrder> remaining;
    remaining.reserve(orders_.size());
    for (auto &ord : orders_) {
        // First consume trade prints at this level (aggressor hits resting maker)
        for (const auto &tp : trades) {
            bool hits = false;
            if (ord.action.side == engine::Side::Buy && tp.side == engine::Side::Sell &&
                tp.price <= ord.price + tick_size_ + 1e-9) {
                hits = true;
            } else if (ord.action.side == engine::Side::Sell && tp.side == engine::Side::Buy &&
                       tp.price >= ord.price - tick_size_ - 1e-9) {
                hits = true;
            }
            if (!hits || ord.my_qty <= 0.0) {
                continue;
            }
            double remaining_trade = tp.size;
            const double burn = std::min(ord.queue_ahead, remaining_trade);
            ord.queue_ahead -= burn;
            remaining_trade -= burn;
            const double fill_qty = std::min(ord.my_qty, remaining_trade);
            ord.my_qty -= fill_qty;
            if (fill_qty > 0.0) {
                engine::Fill f =
                    engine::Fill::filled(ord.action.side, ord.price, fill_qty, ord.my_qty > 0.0, engine::Liquidity::Maker);
                f.order_id = ord.order_id;
                const double penalty = params_.adv_ticks * tick_size_;
                if (ord.action.side == engine::Side::Buy) {
                    f.price += penalty;
                    f.vwap_price += penalty;
                } else {
                    f.price -= penalty;
                    f.vwap_price -= penalty;
                }
                f.unfilled_qty = ord.my_qty;
                f.levels_crossed = 1;
                f.slippage_ticks = 0.0;
                fills.push_back(f);
            }
        }

        const double prev_qty = last_level_qty(ord.price, ord.action.side);
        const double curr_qty = current_level_qty(ord.price, ord.action.side);
        const double delta_down = std::max(0.0, prev_qty - curr_qty);

        if (delta_down > 0.0 && ord.my_qty > 0.0) {
            const double consume_ahead = std::min(ord.queue_ahead, delta_down * params_.alpha);
            ord.queue_ahead -= consume_ahead;
            const double remaining_delta = delta_down - consume_ahead;
            const double fill_qty = std::min(ord.my_qty, remaining_delta);
            ord.my_qty -= fill_qty;
            if (fill_qty > 0.0) {
                engine::Fill f = engine::Fill::filled(ord.action.side, ord.price, fill_qty,
                                                      ord.my_qty > 0.0, engine::Liquidity::Maker);
                f.order_id = ord.order_id;
                const double penalty = params_.adv_ticks * tick_size_;
                if (ord.action.side == engine::Side::Buy) {
                    f.price += penalty;
                    f.vwap_price += penalty;
                } else {
                    f.price -= penalty;
                    f.vwap_price -= penalty;
                }
                f.unfilled_qty = ord.my_qty;
                f.levels_crossed = 1;
                f.slippage_ticks = 0.0;
                fills.push_back(f);
            }
        }

        if (ord.my_qty > 0.0 && now_ts >= ord.expire_ts) {
            continue;
        }
        if (ord.my_qty > 0.0) {
            remaining.push_back(ord);
        }
    }

    orders_.swap(remaining);
    last_bids_ = curr_bids_;
    last_asks_ = curr_asks_;
    return fills;
}

double MakerQueueSim::level_qty(const engine::OrderbookSnapshot &book, double price, engine::Side side) const {
    const auto &levels = (side == engine::Side::Buy) ? book.bids : book.asks;
    for (const auto &lvl : levels) {
        if (std::abs(lvl.price - price) < 1e-9) {
            return lvl.qty;
        }
    }
    if (side == engine::Side::Buy && std::abs(price - book.best_bid) < 1e-9) {
        return book.bid_size;
    }
    if (side == engine::Side::Sell && std::abs(price - book.best_ask) < 1e-9) {
        return book.ask_size;
    }
    return 0.0;
}

void MakerQueueSim::update_level_maps(const engine::OrderbookSnapshot &book) {
    curr_bids_.clear();
    curr_asks_.clear();
    for (const auto &lvl : book.bids) {
        curr_bids_[lvl.price] = lvl.qty;
    }
    for (const auto &lvl : book.asks) {
        curr_asks_[lvl.price] = lvl.qty;
    }
}

double MakerQueueSim::current_level_qty(double price, engine::Side side) const {
    if (side == engine::Side::Buy) {
        auto it = curr_bids_.find(price);
        return it == curr_bids_.end() ? 0.0 : it->second;
    }
    auto it = curr_asks_.find(price);
    return it == curr_asks_.end() ? 0.0 : it->second;
}

double MakerQueueSim::last_level_qty(double price, engine::Side side) const {
    if (side == engine::Side::Buy) {
        auto it = last_bids_.find(price);
        return it == last_bids_.end() ? current_level_qty(price, side) : it->second;
    }
    auto it = last_asks_.find(price);
    return it == last_asks_.end() ? current_level_qty(price, side) : it->second;
}

}  // namespace helix::engine
