#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/types.hpp"

namespace helix::engine {

enum class OrderStatus : uint8_t { New, Partial, Filled, Cancelled, Expired, Replaced, Rejected };

struct Order {
    uint64_t order_id{0};
    Side side{Side::Hold};
    OrderType type{OrderType::Market};
    double price{0.0};
    double qty{0.0};
    double filled_qty{0.0};
    double avg_fill_price{0.0};
    OrderStatus status{OrderStatus::New};
    uint64_t replaced_by{0};
    uint64_t replaced_from{0};
    int64_t created_ts{0};
    int64_t last_update_ts{0};
    int64_t expire_ts{0};
    bool post_only{false};
    bool reduce_only{false};
};

struct OrderMetrics {
    uint64_t orders_placed{0};
    uint64_t orders_cancelled{0};
    uint64_t orders_cancel_noop{0};
    uint64_t orders_rejected{0};
    uint64_t orders_replaced{0};
    uint64_t orders_replace_noop{0};
    uint64_t illegal_transitions{0};
    uint64_t orders_expired{0};
    uint64_t open_orders_peak{0};
    double total_lifetime_ms{0.0};
    uint64_t lifetime_samples{0};
};

struct CancelResult {
    bool success{false};
    bool noop{false};
    std::string message;
};

struct ReplaceResult {
    bool success{false};
    bool noop{false};
    Order new_order;
    std::string message;
};

class OrderManager {
  public:
    OrderManager() = default;

    Order place(const Action &action, int64_t now_ts, int64_t expire_ts);
    CancelResult cancel(uint64_t order_id, int64_t now_ts);
    ReplaceResult replace(uint64_t order_id, double new_price, double new_qty, int64_t now_ts, int64_t expire_ts);
    bool apply_fill(const Fill &fill, int64_t now_ts);
    void mark_rejected(uint64_t order_id, int64_t now_ts);
    void expire_orders(int64_t now_ts);

    const std::unordered_map<uint64_t, Order> &orders() const { return orders_; }
    const OrderMetrics &metrics() const { return metrics_; }
    bool has_error() const { return error_; }
    const std::string &error_message() const { return last_error_; }

  private:
    uint64_t next_order_id_{1};
    std::unordered_map<uint64_t, Order> orders_;
    OrderMetrics metrics_;
    bool error_{false};
    std::string last_error_;

    void update_peak();
    bool set_error(const std::string &msg);
};

}  // namespace helix::engine
