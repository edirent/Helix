#include "engine/tick_replay.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "utils/logger.hpp"

namespace helix::engine {

void TickReplay::load_file(const std::filesystem::path &path) {
    source_ = path;
    cursor_ = 0;

    const bool loaded = std::filesystem::exists(path) && load_csv_from(path);
    if (loaded) {
        utils::info("TickReplay loaded " + std::to_string(snapshots_.size()) + " rows from " + source_.string());
        return;
    }

    seed_synthetic_data();
    utils::warn("TickReplay falling back to synthetic feed; file empty or unreadable: " + source_.string());
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

bool TickReplay::load_csv_from(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    snapshots_.clear();
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) {
            continue;
        }

        // Skip header line if present
        if (line_no == 1 && line.find("best") != std::string::npos) {
            continue;
        }

        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> fields;
        while (std::getline(ss, cell, ',')) {
            fields.push_back(cell);
        }
        if (fields.size() < 4) {
            utils::warn("TickReplay skipped malformed row " + std::to_string(line_no) + " in " + path.string());
            continue;
        }

        // Accept optional leading timestamp column; take the last four numeric fields.
        const std::size_t n = fields.size();
        const double best_bid = std::strtod(fields[n - 4].c_str(), nullptr);
        const double best_ask = std::strtod(fields[n - 3].c_str(), nullptr);
        const double bid_size = std::strtod(fields[n - 2].c_str(), nullptr);
        const double ask_size = std::strtod(fields[n - 1].c_str(), nullptr);
        snapshots_.push_back(OrderbookSnapshot{best_bid, best_ask, bid_size, ask_size});
    }

    cursor_ = 0;
    return !snapshots_.empty();
}

}  // namespace helix::engine
