#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <fstream>

#include "engine/event_bus.hpp"
#include "engine/types.hpp"

namespace helix::engine {

struct BookDelta {
    int64_t seq{0};
    int64_t prev_seq{0};
    bool snapshot{false};
    int64_t ts_ms{0};
    Side side{Side::Hold};
    double price{0.0};
    double qty{0.0};
};

class TickReplay {
  public:
    TickReplay() = default;

    void load_file(const std::filesystem::path &path);
    bool feed_next(EventBus &bus);
    bool finished() const;
    void enable_bookcheck(const std::filesystem::path &path, std::size_t interval = 100);
    bool has_error() const { return error_; }
    const std::string &last_error() const { return last_error_; }
    int64_t current_seq() const { return last_seq_; }
    int64_t current_ts() const { return orderbook_.ts_ms; }
    void load_trades_file(const std::filesystem::path &path);
    std::vector<TradePrint> drain_trades_up_to(int64_t ts_ms);

    const OrderbookSnapshot &current_book() const { return orderbook_; }

  private:
    void seed_synthetic_data();
    bool load_csv_from(const std::filesystem::path &path);
    bool load_delta_csv(std::ifstream &in, const std::vector<std::string> &header_fields);
    bool load_snapshot_csv(std::ifstream &in, const std::vector<std::string> &first_row_fields);
    bool parse_line_fields(const std::string &line, std::vector<std::string> &out_fields) const;
    bool apply_next_delta();
    void rebuild_snapshot_from_maps();
    bool check_invariants(const OrderbookSnapshot &book);
    void maybe_write_bookcheck();
    bool set_error(const std::string &err);

    std::filesystem::path source_;
    std::filesystem::path trades_source_;
    std::vector<OrderbookSnapshot> snapshots_;
    std::vector<BookDelta> deltas_;
    std::vector<TradePrint> trades_;
    std::size_t cursor_{0};
    std::size_t delta_cursor_{0};
    std::size_t trade_cursor_{0};
    int64_t last_seq_{-1};
    int64_t last_ts_ms_{0};
    bool using_deltas_{false};
    bool repeat_seq_{false};
    std::size_t invariant_checks_{0};
    std::size_t invariant_violations_{0};
    std::map<double, double, std::greater<double>> bids_;
    std::map<double, double, std::less<double>> asks_;
    OrderbookSnapshot orderbook_;
    std::deque<BookDelta> recent_deltas_;
    std::ofstream bookcheck_out_;
    std::size_t bookcheck_interval_{0};
    std::size_t bookcheck_counter_{0};
    bool snapshot_in_progress_{false};
    bool error_{false};
    std::string last_error_;
};

}  // namespace helix::engine
