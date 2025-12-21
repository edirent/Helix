#include "engine/order_manager.hpp"

#include <cmath>
#include <sstream>

namespace helix::engine {

Order OrderManager::place(const Action &action, int64_t now_ts, int64_t expire_ts) {
    Order ord;
    ord.order_id = next_order_id_++;
    ord.side = action.side;
    ord.type = action.type;
    ord.price = action.limit_price;
    ord.qty = action.size;
    ord.post_only = action.post_only;
    ord.reduce_only = action.reduce_only;
    ord.status = OrderStatus::New;
    ord.created_ts = now_ts;
    ord.last_update_ts = now_ts;
    ord.expire_ts = expire_ts;
    ord.replaced_from = action.target_order_id;
    orders_[ord.order_id] = ord;
    ++metrics_.orders_placed;
    update_peak();
    return ord;
}

CancelResult OrderManager::cancel(uint64_t order_id, int64_t now_ts) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        ++metrics_.orders_cancel_noop;
        return CancelResult{false, true, "order not found"};
    }
    auto &ord = it->second;
    if (ord.status == OrderStatus::Filled || ord.status == OrderStatus::Cancelled || ord.status == OrderStatus::Expired ||
        ord.status == OrderStatus::Replaced) {
        ++metrics_.orders_cancel_noop;
        return CancelResult{false, true, "order already terminal"};
    }
    ord.status = OrderStatus::Cancelled;
    ord.last_update_ts = now_ts;
    ++metrics_.orders_cancelled;
    metrics_.total_lifetime_ms += static_cast<double>(now_ts - ord.created_ts);
    ++metrics_.lifetime_samples;
    return CancelResult{true, false, ""};
}

ReplaceResult OrderManager::replace(uint64_t order_id, double new_price, double new_qty, int64_t now_ts,
                                    int64_t expire_ts) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        ++metrics_.orders_replace_noop;
        return ReplaceResult{false, true, Order{}, "order not found"};
    }
    auto &ord = it->second;
    if (ord.status == OrderStatus::Filled || ord.status == OrderStatus::Cancelled || ord.status == OrderStatus::Expired ||
        ord.status == OrderStatus::Replaced) {
        ++metrics_.orders_replace_noop;
        return ReplaceResult{false, true, Order{}, "order already terminal"};
    }
    ord.status = OrderStatus::Replaced;
    ord.last_update_ts = now_ts;
    ++metrics_.orders_replaced;
    metrics_.total_lifetime_ms += static_cast<double>(now_ts - ord.created_ts);
    ++metrics_.lifetime_samples;

    Action a;
    a.side = ord.side;
    a.type = ord.type;
    a.limit_price = (new_price > 0.0) ? new_price : ord.price;
    a.size = (new_qty > 0.0) ? new_qty : (ord.qty - ord.filled_qty);
    a.post_only = ord.post_only;
    a.reduce_only = ord.reduce_only;
    a.target_order_id = ord.order_id;
    Order new_ord = place(a, now_ts, expire_ts);
    ord.replaced_by = new_ord.order_id;
    return ReplaceResult{true, false, new_ord, ""};
}

bool OrderManager::apply_fill(const Fill &fill, int64_t now_ts) {
    auto it = orders_.find(fill.order_id);
    if (it == orders_.end()) {
        std::stringstream ss;
        ss << "fill for unknown order_id=" << fill.order_id;
        set_error(ss.str());
        return false;
    }
    auto &ord = it->second;
    if (ord.status == OrderStatus::Cancelled || ord.status == OrderStatus::Expired || ord.status == OrderStatus::Replaced ||
        ord.status == OrderStatus::Filled) {
        std::stringstream ss;
        ss << "illegal fill on terminal order_id=" << ord.order_id << " status=" << static_cast<int>(ord.status);
        set_error(ss.str());
        ++metrics_.illegal_transitions;
        return false;
    }
    if (fill.side != ord.side) {
        set_error("fill side mismatch for order_id=" + std::to_string(ord.order_id));
        ++metrics_.illegal_transitions;
        return false;
    }
    const double prev_filled = ord.filled_qty;
    const double new_filled = prev_filled + fill.filled_qty;
    if (new_filled > ord.qty + 1e-6) {
        set_error("overfill detected for order_id=" + std::to_string(ord.order_id));
        ++metrics_.illegal_transitions;
        return false;
    }
    ord.filled_qty = new_filled;
    const double total_notional = ord.avg_fill_price * prev_filled + fill.vwap_price * fill.filled_qty;
    if (new_filled > 0.0) {
        ord.avg_fill_price = total_notional / new_filled;
    }
    ord.last_update_ts = now_ts;
    if (new_filled + 1e-9 >= ord.qty) {
        ord.status = OrderStatus::Filled;
        metrics_.total_lifetime_ms += static_cast<double>(now_ts - ord.created_ts);
        ++metrics_.lifetime_samples;
    } else {
        ord.status = OrderStatus::Partial;
    }
    return true;
}

void OrderManager::mark_rejected(uint64_t order_id, int64_t now_ts) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return;
    }
    auto &ord = it->second;
    if (ord.status == OrderStatus::New || ord.status == OrderStatus::Partial) {
        ord.status = OrderStatus::Rejected;
        ord.last_update_ts = now_ts;
        ++metrics_.orders_rejected;
        metrics_.total_lifetime_ms += static_cast<double>(now_ts - ord.created_ts);
        ++metrics_.lifetime_samples;
    }
}

void OrderManager::expire_orders(int64_t now_ts) {
    for (auto &kv : orders_) {
        auto &ord = kv.second;
        if (ord.status == OrderStatus::New || ord.status == OrderStatus::Partial) {
            if (ord.expire_ts > 0 && now_ts >= ord.expire_ts) {
                ord.status = OrderStatus::Expired;
                ord.last_update_ts = now_ts;
                ++metrics_.orders_expired;
                metrics_.total_lifetime_ms += static_cast<double>(now_ts - ord.created_ts);
                ++metrics_.lifetime_samples;
            }
        }
    }
}

bool OrderManager::set_error(const std::string &msg) {
    error_ = true;
    last_error_ = msg;
    return false;
}

void OrderManager::update_peak() {
    uint64_t open = 0;
    for (const auto &kv : orders_) {
        const auto st = kv.second.status;
        if (st == OrderStatus::New || st == OrderStatus::Partial) {
            ++open;
        }
    }
    if (open > metrics_.open_orders_peak) {
        metrics_.open_orders_peak = open;
    }
}

}  // namespace helix::engine
