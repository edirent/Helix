#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "engine/event_bus.hpp"
#include "engine/types.hpp"

namespace helix::engine {

class TickReplay {
  public:
    TickReplay() = default;

    void load_file(const std::filesystem::path &path);
    bool feed_next(EventBus &bus);
    bool finished() const;

    const OrderbookSnapshot &current_book() const { return orderbook_; }

  private:
    void seed_synthetic_data();
    bool load_csv_from(const std::filesystem::path &path);

    std::filesystem::path source_;
    std::vector<OrderbookSnapshot> snapshots_;
    std::size_t cursor_{0};
    OrderbookSnapshot orderbook_;
};

}  // namespace helix::engine
