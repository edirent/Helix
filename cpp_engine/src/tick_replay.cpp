#include "engine/tick_replay.hpp"

#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>

#include "utils/logger.hpp"

namespace helix::engine {
namespace {

OrderbookSnapshot make_single_level_book(int64_t ts_ms, double best_bid, double best_ask, double bid_size, double ask_size) {
    OrderbookSnapshot snap;
    snap.ts_ms = ts_ms;
    snap.best_bid = best_bid;
    snap.best_ask = best_ask;
    snap.bid_size = bid_size;
    snap.ask_size = ask_size;
    if (best_bid > 0.0 && bid_size > 0.0) {
        snap.bids.push_back(PriceLevel{best_bid, bid_size});
    }
    if (best_ask > 0.0 && ask_size > 0.0) {
        snap.asks.push_back(PriceLevel{best_ask, ask_size});
    }
    return snap;
}

bool contains_alpha(const std::vector<std::string> &fields) {
    for (const auto &f : fields) {
        for (char c : f) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                return true;
            }
        }
    }
    return false;
}

bool contains_token(const std::vector<std::string> &fields, const std::string &token) {
    for (const auto &f : fields) {
        if (f == token) {
            return true;
        }
    }
    return false;
}

}  // namespace

void TickReplay::load_file(const std::filesystem::path &path) {
    source_ = path;
    cursor_ = 0;
    delta_cursor_ = 0;
    last_seq_ = -1;
    last_ts_ms_ = 0;
    using_deltas_ = false;
    bids_.clear();
    asks_.clear();
    deltas_.clear();

    const bool loaded = std::filesystem::exists(path) && load_csv_from(path);
    if (loaded) {
        const std::size_t count = using_deltas_ ? deltas_.size() : snapshots_.size();
        utils::info("TickReplay loaded " + std::to_string(count) + " rows from " + source_.string());
        return;
    }

    seed_synthetic_data();
    utils::warn("TickReplay falling back to synthetic feed; file empty or unreadable: " + source_.string());
}

void TickReplay::seed_synthetic_data() {
    snapshots_.clear();
    // Basic synthetic book trajectory.
    for (int i = 0; i < 5; ++i) {
        const double best_bid = 100.0 + i * 0.1;
        const double best_ask = 100.5 + i * 0.1;
        const double bid_size = 10.0 + i;
        const double ask_size = 12.0 - i * 0.5;
        const int64_t ts_ms = 1000 + i * 100;
        snapshots_.push_back(make_single_level_book(ts_ms, best_bid, best_ask, bid_size, ask_size));
        last_ts_ms_ = ts_ms;
    }
}

bool TickReplay::feed_next(EventBus &bus) {
    if (using_deltas_) {
        if (!apply_next_delta()) {
            return false;
        }
    } else {
        if (cursor_ >= snapshots_.size()) {
            return false;
        }
        orderbook_ = snapshots_[cursor_++];
    }

    std::stringstream ss;
    ss << "bid=" << orderbook_.best_bid << " ask=" << orderbook_.best_ask;
    Event evt;
    evt.type = Event::Type::Tick;
    evt.payload = ss.str();
    return bus.publish(evt);
}

bool TickReplay::finished() const {
    if (using_deltas_) {
        return delta_cursor_ >= deltas_.size();
    }
    return cursor_ >= snapshots_.size();
}

bool TickReplay::load_csv_from(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    snapshots_.clear();
    deltas_.clear();

    std::string first_line;
    if (!std::getline(in, first_line)) {
        return false;
    }
    std::vector<std::string> first_fields;
    if (!parse_line_fields(first_line, first_fields)) {
        return false;
    }

    const bool header_like = contains_alpha(first_fields);
    const bool looks_delta = contains_token(first_fields, "seq") || contains_token(first_fields, "type") ||
                             contains_token(first_fields, "side");

    if (looks_delta) {
        using_deltas_ = true;
        std::vector<std::string> header_fields = header_like ? first_fields : std::vector<std::string>{};
        if (!header_like) {
            // rewind to include first line as data
            in.seekg(0);
        }
        const bool ok = load_delta_csv(in, header_fields);
        delta_cursor_ = 0;
        return ok;
    }

    // Snapshot style (legacy) CSV: optional header + best bid/ask + sizes.
    using_deltas_ = false;
    std::size_t line_no = 0;
    std::vector<std::string> fields = first_fields;
    while (true) {
        ++line_no;
        if (fields.empty()) {
            // nothing to do
        } else if (line_no == 1 && header_like) {
            // skip header
        } else if (fields.size() < 4) {
            utils::warn("TickReplay skipped malformed row " + std::to_string(line_no) + " in " + path.string());
        } else {
            const std::size_t n = fields.size();
            int64_t ts_ms = last_ts_ms_ + 1;
            char *ts_end = nullptr;
            if (!header_like && !fields.empty()) {
                ts_ms = std::strtoll(fields[0].c_str(), &ts_end, 10);
                if (ts_end == fields[0].c_str()) {
                    ts_ms = last_ts_ms_ + 1;
                }
            }
            const double best_bid = std::strtod(fields[n - 4].c_str(), nullptr);
            const double best_ask = std::strtod(fields[n - 3].c_str(), nullptr);
            const double bid_size = std::strtod(fields[n - 2].c_str(), nullptr);
            const double ask_size = std::strtod(fields[n - 1].c_str(), nullptr);
            snapshots_.push_back(make_single_level_book(ts_ms, best_bid, best_ask, bid_size, ask_size));
            last_ts_ms_ = ts_ms;
        }

        std::string line;
        if (!std::getline(in, line)) {
            break;
        }
        if (!parse_line_fields(line, fields)) {
            fields.clear();
        }
    }

    cursor_ = 0;
    return !snapshots_.empty();
}

bool TickReplay::parse_line_fields(const std::string &line, std::vector<std::string> &out_fields) const {
    out_fields.clear();
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
        out_fields.push_back(cell);
    }
    return !out_fields.empty();
}

bool TickReplay::load_delta_csv(std::ifstream &in, const std::vector<std::string> &header_fields) {
    deltas_.clear();
    std::vector<std::string> headers = header_fields;
    bool header_known = !headers.empty();
    std::string line;
    std::size_t line_no = 0;

    auto idx = [&](const std::string &name) -> int {
        if (!header_known) {
            return -1;
        }
        for (std::size_t i = 0; i < headers.size(); ++i) {
            if (headers[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> fields;
        if (!parse_line_fields(line, fields)) {
            continue;
        }
        if (!header_known && line_no == 1 && contains_alpha(fields)) {
            headers = fields;
            header_known = true;
            continue;
        }

        const int ts_idx = idx("ts_ms");
        const int seq_idx = idx("seq");
        const int prev_idx = idx("prev_seq");
        const int type_idx = idx("type");
        const int side_idx = idx("book_side") >= 0 ? idx("book_side") : idx("side");
        const int price_idx = idx("price");
        const int size_idx = idx("size");

        auto get_str = [&](int i) -> std::string {
            if (i < 0 || static_cast<std::size_t>(i) >= fields.size()) {
                return "";
            }
            return fields[static_cast<std::size_t>(i)];
        };
        auto get_double = [&](int i, double def) -> double {
            if (i < 0 || static_cast<std::size_t>(i) >= fields.size()) {
                return def;
            }
            return std::strtod(fields[static_cast<std::size_t>(i)].c_str(), nullptr);
        };
        auto get_int64 = [&](int i, int64_t def) -> int64_t {
            if (i < 0 || static_cast<std::size_t>(i) >= fields.size()) {
                return def;
            }
            return std::strtoll(fields[static_cast<std::size_t>(i)].c_str(), nullptr, 10);
        };

        // Fallback positional indices if no headers.
        const bool use_positional = !header_known;
        const std::size_t n = fields.size();
        const std::size_t pos_ts = 0;
        const std::size_t pos_seq = (n > 1) ? 1 : 0;
        const std::size_t pos_prev = (n > 2) ? 2 : 0;
        const std::size_t pos_type = (n > 3) ? 3 : 0;
        const std::size_t pos_side = (n > 4) ? 4 : 0;
        const std::size_t pos_price = (n > 5) ? 5 : 0;
        const std::size_t pos_size = (n > 6) ? 6 : 0;

        BookDelta delta;
        delta.ts_ms =
            use_positional ? ((n > pos_ts) ? std::strtoll(fields[pos_ts].c_str(), nullptr, 10) : 0)
                           : get_int64(ts_idx, 0);
        delta.seq = use_positional ? std::strtoll(fields[pos_seq].c_str(), nullptr, 10) : get_int64(seq_idx, -1);
        delta.prev_seq =
            use_positional ? std::strtoll(fields[pos_prev].c_str(), nullptr, 10) : get_int64(prev_idx, -1);
        const std::string type = use_positional ? ((n > pos_type) ? fields[pos_type] : "") : get_str(type_idx);
        const std::string side = use_positional ? ((n > pos_side) ? fields[pos_side] : "") : get_str(side_idx);
        delta.snapshot = (type == "snapshot" || type == "snap" || type == "full");
        const std::string side_lower = side.empty() ? "" : std::string(side);
        if (!side_lower.empty()) {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(side_lower[0])));
            if (c == 'b') {
                delta.side = Side::Buy;  // bid
            } else if (c == 'a') {
                delta.side = Side::Sell;  // ask
            } else {
                continue;  // invalid book_side
            }
        }
        delta.price =
            use_positional ? ((n > pos_price) ? std::strtod(fields[pos_price].c_str(), nullptr) : 0.0)
                           : get_double(price_idx, 0.0);
        delta.qty =
            use_positional ? ((n > pos_size) ? std::strtod(fields[pos_size].c_str(), nullptr) : 0.0)
                           : get_double(size_idx, 0.0);

        if (delta.side == Side::Hold) {
            continue;
        }
        deltas_.push_back(delta);
    }

    return !deltas_.empty();
}

bool TickReplay::apply_next_delta() {
    if (delta_cursor_ >= deltas_.size()) {
        return false;
    }
    const auto &d = deltas_[delta_cursor_++];

    if (d.snapshot) {
        bids_.clear();
        asks_.clear();
    } else {
        if (last_seq_ >= 0 && d.prev_seq > 0 && d.prev_seq != last_seq_) {
            utils::warn("TickReplay detected seq gap: prev=" + std::to_string(last_seq_) +
                        " next_prev=" + std::to_string(d.prev_seq));
            return false;
        }
    }
    last_seq_ = d.seq;
    if (d.ts_ms > 0) {
        last_ts_ms_ = d.ts_ms;
    } else {
        last_ts_ms_ += 1;
    }

    constexpr double kEps = 1e-9;
    if (d.qty < 0.0) {
        utils::warn("TickReplay negative qty delta at seq=" + std::to_string(d.seq));
        return false;
    }

    if (d.side == Side::Buy) {
        if (std::abs(d.qty) < kEps) {
            bids_.erase(d.price);
        } else {
            bids_[d.price] = d.qty;
        }
    } else {
        if (std::abs(d.qty) < kEps) {
            asks_.erase(d.price);
        } else {
            asks_[d.price] = d.qty;
        }
    }

    rebuild_snapshot_from_maps();
    return true;
}

void TickReplay::rebuild_snapshot_from_maps() {
    orderbook_.bids.clear();
    orderbook_.asks.clear();
    orderbook_.ts_ms = last_ts_ms_;
    orderbook_.best_bid = 0.0;
    orderbook_.best_ask = 0.0;
    orderbook_.bid_size = 0.0;
    orderbook_.ask_size = 0.0;

    for (auto it = bids_.begin(); it != bids_.end();) {
        if (it->second <= 0.0) {
            it = bids_.erase(it);
            continue;
        }
        orderbook_.bids.push_back(PriceLevel{it->first, it->second});
        if (orderbook_.best_bid == 0.0) {
            orderbook_.best_bid = it->first;
            orderbook_.bid_size = it->second;
        }
        ++it;
    }
    for (auto it = asks_.begin(); it != asks_.end();) {
        if (it->second <= 0.0) {
            it = asks_.erase(it);
            continue;
        }
        orderbook_.asks.push_back(PriceLevel{it->first, it->second});
        if (orderbook_.best_ask == 0.0) {
            orderbook_.best_ask = it->first;
            orderbook_.ask_size = it->second;
        }
        ++it;
    }
}

}  // namespace helix::engine
