#pragma once

#include <map>
#include <vector>

#include "engine/types.hpp"

namespace helix::engine {

struct MakerParams {
    double q_init{0.8};
    double alpha{0.6};
    int64_t expire_ms{200};
    double adv_ticks{2.0};
};

struct RestingOrder {
    engine::Action action;
    uint64_t order_id{0};
    double price{0.0};
    double queue_ahead{0.0};
    double my_qty{0.0};
    int64_t submit_ts{0};
    int64_t expire_ts{0};
};

class MakerQueueSim {
  public:
    MakerQueueSim(MakerParams params, double tick_size);

    void submit(const engine::Action &action, const engine::OrderbookSnapshot &book, int64_t now_ts);
    std::vector<engine::Fill> on_book(const engine::OrderbookSnapshot &book, int64_t now_ts,
                                      const std::vector<engine::TradePrint> &trades);
    bool cancel(uint64_t order_id);

  private:
    double level_qty(const engine::OrderbookSnapshot &book, double price, engine::Side side) const;
    void update_level_maps(const engine::OrderbookSnapshot &book);
    double current_level_qty(double price, engine::Side side) const;
    double last_level_qty(double price, engine::Side side) const;

    MakerParams params_;
    std::vector<RestingOrder> orders_;
    std::map<double, double, std::greater<double>> last_bids_;
    std::map<double, double, std::less<double>> last_asks_;
    std::map<double, double, std::greater<double>> curr_bids_;
    std::map<double, double, std::less<double>> curr_asks_;
    double tick_size_{0.0};
};

}  // namespace helix::engine
