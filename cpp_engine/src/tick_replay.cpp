#include "engine/tick_replay.hpp"

#include <sstream>

#include "utils/logger.hpp"

namespace helix::engine {

void TickReplay::load_file(const std::filesystem::path &path) {
    source_ = path;
    seed_synthetic_data();
    cursor_ = 0;
    utils::info("TickReplay loaded synthetic feed from " + source_.string());
}

void TickReplay::seed_synthetic_data() {
    snapshots_.clear();
    // Basic synthetic book trajectory.
    for (int i = 0; i < 5; ++i) {
        OrderbookSnapshot snap;
        snap.best_bid = 100.0 + i * 0.1;
        snap.best_ask = 100.5 + i * 0.1;
        snap.bid_size = 10.0 + i;
        snap.ask_size = 12.0 - i * 0.5;
        snapshots_.push_back(snap);
    }
}

bool TickReplay::feed_next(EventBus &bus) {
    if (cursor_ >= snapshots_.size()) {
        return false;
    }
    orderbook_ = snapshots_[cursor_++];
    std::stringstream ss;
    ss << "bid=" << orderbook_.best_bid << " ask=" << orderbook_.best_ask;
    Event evt;
    evt.type = Event::Type::Tick;
    evt.payload = ss.str();
    return bus.publish(evt);
}

bool TickReplay::finished() const { return cursor_ >= snapshots_.size(); }

}  // namespace helix::engine
