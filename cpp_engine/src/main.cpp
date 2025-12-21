#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>
#include <optional>

#include "engine/fee_model.hpp"
#include "engine/decision_engine.hpp"
#include "engine/latency.hpp"
#include "engine/event_bus.hpp"
#include "engine/feature_engine.hpp"
#include "engine/matching_engine.hpp"
#include "engine/rules_engine.hpp"
#include "engine/order_utils.hpp"
#include "engine/maker_queue.hpp"
#include "engine/recorder.hpp"
#include "engine/risk_engine.hpp"
#include "engine/order_manager.hpp"
#include "engine/tick_replay.hpp"
#include "transport/grpc_server.hpp"
#include "transport/zmq_server.hpp"
#include "utils/logger.hpp"

using namespace helix;

namespace {
struct PendingAction {
    engine::Action action;
    int64_t fill_ts{0};
    uint64_t seq{0};
    uint64_t action_idx{0};
    bool demo{false};
    double target_notional{0.0};
    bool crossing{false};
    uint64_t order_id{0};
};

struct PnLAggregate {
    double gross{0.0};
    double fees{0.0};
    std::vector<double> net_steps;
    std::map<int64_t, double> net_by_1s;
    std::map<int64_t, double> net_by_10s;
    std::vector<double> maker_queue_times_ms;
    std::vector<double> maker_adv_ticks;
    std::vector<double> latency_samples_ms;
    std::vector<double> trade_skews_ms;
    std::vector<double> fee_bps_samples;
    std::vector<double> fee_bps_maker_samples;
    std::vector<double> fee_bps_taker_samples;
    std::vector<double> exec_cost_ticks_signed_samples;
    std::vector<double> exec_cost_ticks_signed_maker_samples;
    std::vector<double> exec_cost_ticks_signed_taker_samples;
    std::vector<double> filled_to_target_samples;
    int maker_orders_submitted{0};
    double net() const { return gross - fees; }
    double turnover{0.0};
    int fills_total{0};
    int maker_fills{0};
    int taker_fills{0};
    int rejects_total{0};
    int actions_attempted{0};
    std::unordered_map<std::string, int> reject_counts;
    struct SharpeStats {
        double mean{0.0};
        double std{0.0};
        std::size_t n{0};
        double sharpe{0.0};
    };
    static SharpeStats sharpe_from_buckets(const std::map<int64_t, double> &buckets) {
        SharpeStats s;
        s.n = buckets.size();
        if (s.n < 2) {
            return s;  // insufficient data; sharpe left as 0
        }
        for (const auto &kv : buckets) {
            s.mean += kv.second;
        }
        s.mean /= static_cast<double>(s.n);
        double var = 0.0;
        for (const auto &kv : buckets) {
            const double diff = kv.second - s.mean;
            var += diff * diff;
        }
        var /= static_cast<double>(s.n - 1);
        s.std = std::sqrt(var);
        if (s.std > 1e-9) {
            s.sharpe = s.mean / s.std * std::sqrt(static_cast<double>(s.n));
        }
        return s;
    }
    double max_drawdown() const {
        double equity = 0.0;
        double peak = 0.0;
        double max_dd = 0.0;
        for (double step : net_steps) {
            equity += step;
            if (equity > peak) {
                peak = equity;
            }
            const double dd = peak - equity;
            if (dd > max_dd) {
                max_dd = dd;
            }
        }
        return max_dd;
    }
    double fill_rate() const {
        const int denom = fills_total + rejects_total;
        if (denom == 0) {
            return 0.0;
        }
        return static_cast<double>(fills_total) / static_cast<double>(denom);
    }
    double maker_fill_rate() const {
        if (maker_orders_submitted <= 0) {
            return 0.0;
        }
        return static_cast<double>(maker_fills) / static_cast<double>(maker_orders_submitted);
    }
};

struct PendingComparator {
    bool operator()(const PendingAction &a, const PendingAction &b) const { return a.fill_ts > b.fill_ts; }
};

struct PendingMakerAdv {
    double mid_at_fill{0.0};
    double fill_vwap{0.0};
    engine::Side side{engine::Side::Hold};
    std::size_t fill_row_index{0};
    int64_t target_ts_ms{0};
};

double percentile(const std::vector<double> &values, double pct) {
    if (values.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double rank = (pct / 100.0) * (static_cast<double>(sorted.size()) - 1.0);
    const std::size_t idx = static_cast<std::size_t>(std::ceil(rank));
    return sorted[std::min(idx, sorted.size() - 1)];
}

double sample_stddev(const std::vector<double> &values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double mean = 0.0;
    for (double v : values) {
        mean += v;
    }
    mean /= static_cast<double>(values.size());
    double var = 0.0;
    for (double v : values) {
        const double d = v - mean;
        var += d * d;
    }
    var /= static_cast<double>(values.size() - 1);
    return std::sqrt(var);
}

std::string side_str(engine::Side side) {
    switch (side) {
        case engine::Side::Buy:
            return "BUY";
        case engine::Side::Sell:
            return "SELL";
        default:
            return "HOLD";
    }
}

std::string liquidity_str(engine::Liquidity liq) { return liq == engine::Liquidity::Maker ? "MAKER" : "TAKER"; }

std::string status_str(engine::FillStatus st) { return st == engine::FillStatus::Filled ? "filled" : "rejected"; }

std::string reason_str(engine::RejectReason r) {
    switch (r) {
        case engine::RejectReason::None:
            return "None";
        case engine::RejectReason::BadSide:
            return "BadSide";
        case engine::RejectReason::ZeroQty:
            return "ZeroQty";
        case engine::RejectReason::NoBid:
            return "NoBid";
        case engine::RejectReason::NoAsk:
            return "NoAsk";
        case engine::RejectReason::NoLiquidity:
            return "NoLiquidity";
        case engine::RejectReason::MinQty:
            return "MinQty";
        case engine::RejectReason::MinNotional:
            return "MinNotional";
        case engine::RejectReason::PriceInvalid:
            return "PriceInvalid";
        case engine::RejectReason::RiskLimit:
            return "RiskLimit";
        default:
            return "Unknown";
    }
}

struct FillRow {
    uint64_t order_id{0};
    int64_t ts_ms{0};
    int64_t seq{0};
    std::string status;
    std::string side;
    std::string liquidity;
    std::string src;
    std::string reason;
    double vwap{0.0};
    double filled_qty{0.0};
    double unfilled_qty{0.0};
    double fee{0.0};
    double fee_bps{0.0};
    double gross{0.0};
    double net{0.0};
    double exec_cost_ticks_signed{0.0};
    double mid{0.0};
    double best{0.0};
    double spread_paid_ticks{0.0};
    double slip_ticks{0.0};
    double target_notional{0.0};
    double filled_notional{0.0};
    int crossing{0};
    int levels_crossed{0};
    double adv_ticks{0.0};
    double queue_time_ms{0.0};
    double adv_selection_ticks{0.0};
};

std::string generate_run_id(const std::string &override_id = "") {
    if (!override_id.empty()) {
        return override_id;
    }
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::mt19937_64 rng(static_cast<uint64_t>(ms) ^ std::random_device{}());
    const uint64_t salt = rng() % 1000000;
    std::stringstream ss;
    ss << "run_" << ms << "_" << salt;
    return ss.str();
}

bool load_config_from_yaml(const std::filesystem::path &path, const std::string &venue, const std::string &symbol,
                           engine::RulesConfig &rules_cfg, engine::FeeConfig &fee_cfg) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::string line;
    bool in_venue = false;
    bool in_symbol = false;
    bool in_fee = false;
    auto ltrim = [](const std::string &s) {
        std::size_t start = s.find_first_not_of(" \t");
        return (start == std::string::npos) ? std::string() : s.substr(start);
    };
    auto keyval = [](const std::string &s, std::string &key, std::string &val) {
        auto pos = s.find(":");
        if (pos == std::string::npos) {
            return false;
        }
        key = s.substr(0, pos);
        val = s.substr(pos + 1);
        return true;
    };
    auto strip_quotes = [](const std::string &s) {
        std::size_t start = 0;
        std::size_t end = s.size();
        while (start < end && (s[start] == '"' || s[start] == '\'')) {
            ++start;
        }
        while (end > start && (s[end - 1] == '"' || s[end - 1] == '\'')) {
            --end;
        }
        return s.substr(start, end - start);
    };
    while (std::getline(in, line)) {
        std::string trimmed = ltrim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.back() == '\r') {
            trimmed.pop_back();
        }
        if (trimmed == venue + ":") {
            in_venue = true;
            in_symbol = false;
            in_fee = false;
            continue;
        }
        if (in_venue && trimmed == symbol + ":") {
            in_symbol = true;
            in_fee = false;
            continue;
        }
        if (!in_symbol) {
            continue;
        }
        if (trimmed.rfind("fee:", 0) == 0) {
            in_fee = true;
            continue;
        }
        std::string key, val;
        if (!keyval(trimmed, key, val)) {
            continue;
        }
        key = ltrim(key);
        val = ltrim(val);
        if (in_fee) {
            if (key == "maker_bps") {
                fee_cfg.maker_bps = std::stod(val);
            } else if (key == "taker_bps") {
                fee_cfg.taker_bps = std::stod(val);
            } else if (key == "fee_ccy") {
                fee_cfg.fee_ccy = strip_quotes(val);
            } else if (key == "rounding") {
                fee_cfg.rounding = strip_quotes(val);
            }
        } else {
            if (key == "tick_size") {
                rules_cfg.tick_size = std::stod(val);
            } else if (key == "qty_step") {
                rules_cfg.qty_step = std::stod(val);
            } else if (key == "min_qty") {
                rules_cfg.min_qty = std::stod(val);
            } else if (key == "min_notional") {
                rules_cfg.min_notional = std::stod(val);
            }
        }
    }
    return true;
}

bool load_latency_fit(const std::filesystem::path &path, engine::LatencyConfig &cfg) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();
    auto extract = [&](const std::string &key, double &out) -> bool {
        const auto pos = content.find(key);
        if (pos == std::string::npos) {
            return false;
        }
        const auto colon = content.find(":", pos);
        if (colon == std::string::npos) {
            return false;
        }
        const std::string tail = content.substr(colon + 1);
        try {
            out = std::stod(tail);
            return true;
        } catch (...) {
            return false;
        }
    };
    extract("base_ms", cfg.base_ms);
    extract("jitter_ms", cfg.jitter_ms);
    extract("tail_ms", cfg.tail_ms);
    extract("tail_prob", cfg.tail_prob);
    cfg.source = "file:" + path.string();
    return true;
}
bool write_fills_csv(const std::filesystem::path &path, const std::vector<FillRow> &rows) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << "order_id,ts_ms,seq,status,side,liquidity,src,reason,vwap,filled_qty,unfilled_qty,fee,fee_bps,gross,net,"
           "exec_cost_ticks_signed,mid,best,spread_paid_ticks,slip_ticks,target_notional,filled_notional,crossing,levels_crossed,adv_ticks,queue_time_ms,adv_selection_ticks\n";
    out << std::setprecision(10);
    for (const auto &r : rows) {
        out << r.order_id << "," << r.ts_ms << "," << r.seq << "," << r.status << "," << r.side << "," << r.liquidity
            << "," << r.src << "," << r.reason << "," << r.vwap << "," << r.filled_qty << "," << r.unfilled_qty << ","
            << r.fee << "," << r.fee_bps << "," << r.gross << "," << r.net << "," << r.exec_cost_ticks_signed << ","
            << r.mid << "," << r.best << "," << r.spread_paid_ticks << "," << r.slip_ticks << ","
            << r.target_notional << "," << r.filled_notional << "," << r.crossing << "," << r.levels_crossed << ","
            << r.adv_ticks << "," << r.queue_time_ms << "," << r.adv_selection_ticks << "\n";
    }
    return true;
}

bool write_metrics_json(const std::filesystem::path &path, const std::string &run_id, const PnLAggregate &pnl,
                        double realized, double unrealized, double net_total, const PnLAggregate::SharpeStats &s1,
                        const PnLAggregate::SharpeStats &s10, double max_dd, double fill_rate, double maker_fill_rate,
                        double maker_queue_avg, double maker_queue_p90, double maker_adv_mean, double maker_adv_p90,
                        const std::unordered_map<std::string, int> &reject_counts, bool identity_ok,
                        const engine::RulesConfig &rules_cfg, const engine::FeeConfig &fee_cfg,
                        const engine::OrderMetrics &order_metrics, double avg_lifetime_ms,
                        const engine::LatencyConfig &lat_cfg, double lat_p50, double lat_p90, double lat_p99,
                        std::size_t lat_n, double trade_skew_p50, double trade_skew_p90, double trade_skew_p99,
                        std::size_t trade_skew_n, std::size_t maker_adv_count, double fee_bps_p50, double fee_bps_p99,
                        double exec_cost_p50, double exec_cost_p99, double exec_cost_std, double filled_to_target_p99,
                        double fee_bps_maker_p50, double fee_bps_maker_p90, double fee_bps_maker_p99,
                        double fee_bps_taker_p50, double fee_bps_taker_p90, double fee_bps_taker_p99,
                        double exec_cost_maker_p50, double exec_cost_maker_p99, double exec_cost_maker_std,
                        double exec_cost_taker_p50, double exec_cost_taker_p99, double exec_cost_taker_std) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << std::setprecision(10);
    out << "{\n";
    out << "  \"run_id\": \"" << run_id << "\",\n";
    out << "  \"fees\": " << pnl.fees << ",\n";
    out << "  \"gross\": " << pnl.gross << ",\n";
    out << "  \"realized\": " << realized << ",\n";
    out << "  \"unrealized\": " << unrealized << ",\n";
    out << "  \"net_total\": " << net_total << ",\n";
    out << "  \"identity_ok\": " << std::boolalpha << identity_ok << ",\n";
    out << "  \"sharpe_1s\": {\"sharpe\": " << s1.sharpe << ", \"n\": " << s1.n << ", \"std\": " << s1.std << "},\n";
    out << "  \"sharpe_10s\": {\"sharpe\": " << s10.sharpe << ", \"n\": " << s10.n << ", \"std\": " << s10.std
        << "},\n";
    out << "  \"max_drawdown\": " << max_dd << ",\n";
    out << "  \"turnover\": " << pnl.turnover << ",\n";
    out << "  \"fill_rate\": " << fill_rate << ",\n";
    out << "  \"maker_fill_rate\": " << maker_fill_rate << ",\n";
    out << "  \"maker_queue_time_ms\": {\"avg\": " << maker_queue_avg << ", \"p90\": " << maker_queue_p90 << "},\n";
    out << "  \"maker_adv_selection_ticks\": {\"mean\": " << maker_adv_mean << ", \"p90\": " << maker_adv_p90
        << ", \"count\": " << maker_adv_count << "},\n";
    out << "  \"trade_ts_skew_ms\": {\"p50\": " << trade_skew_p50 << ", \"p90\": " << trade_skew_p90
        << ", \"p99\": " << trade_skew_p99 << ", \"n\": " << trade_skew_n << "},\n";
    out << "  \"fee_bps\": {\"p50\": " << fee_bps_p50 << ", \"p99\": " << fee_bps_p99 << "},\n";
    out << "  \"fee_bps_maker\": {\"p50\": " << fee_bps_maker_p50 << ", \"p90\": " << fee_bps_maker_p90
        << ", \"p99\": " << fee_bps_maker_p99 << ", \"n\": " << pnl.maker_fills << "},\n";
    out << "  \"fee_bps_taker\": {\"p50\": " << fee_bps_taker_p50 << ", \"p90\": " << fee_bps_taker_p90
        << ", \"p99\": " << fee_bps_taker_p99 << ", \"n\": " << pnl.taker_fills << "},\n";
    out << "  \"exec_cost_ticks_signed\": {\"p50\": " << exec_cost_p50 << ", \"p99\": " << exec_cost_p99
        << ", \"std\": " << exec_cost_std << "},\n";
    out << "  \"exec_cost_ticks_signed_maker\": {\"p50\": " << exec_cost_maker_p50 << ", \"p99\": "
        << exec_cost_maker_p99 << ", \"std\": " << exec_cost_maker_std << ", \"n\": " << pnl.maker_fills << "},\n";
    out << "  \"exec_cost_ticks_signed_taker\": {\"p50\": " << exec_cost_taker_p50 << ", \"p99\": "
        << exec_cost_taker_p99 << ", \"std\": " << exec_cost_taker_std << ", \"n\": " << pnl.taker_fills << "},\n";
    out << "  \"filled_to_target\": {\"p99\": " << filled_to_target_p99 << "},\n";
    out << "  \"fills_total\": " << pnl.fills_total << ",\n";
    out << "  \"makers\": " << pnl.maker_fills << ",\n";
    out << "  \"takers\": " << pnl.taker_fills << ",\n";
    out << "  \"n_maker_fills\": " << pnl.maker_fills << ",\n";
    out << "  \"n_taker_fills\": " << pnl.taker_fills << ",\n";
    out << "  \"rejects_total\": " << pnl.rejects_total << ",\n";
    out << "  \"actions_attempted\": " << pnl.actions_attempted << ",\n";
    out << "  \"reject_counts\": {\n";
    std::size_t idx = 0;
    const std::size_t total = reject_counts.size();
    for (const auto &kv : reject_counts) {
        out << "    \"" << kv.first << "\": " << kv.second;
        if (++idx < total) {
            out << ",";
        }
        out << "\n";
    }
    out << "  },\n";
    out << "  \"rules\": {\"tick_size\": " << rules_cfg.tick_size << ", \"qty_step\": " << rules_cfg.qty_step
        << ", \"min_qty\": " << rules_cfg.min_qty << ", \"min_notional\": " << rules_cfg.min_notional
        << ", \"source\": \"" << rules_cfg.source << "\"},\n";
    out << "  \"fee_model\": {\"maker_bps\": " << fee_cfg.maker_bps << ", \"taker_bps\": " << fee_cfg.taker_bps
        << ", \"fee_ccy\": \"" << fee_cfg.fee_ccy << "\", \"rounding\": \"" << fee_cfg.rounding
        << "\", \"source\": \"" << fee_cfg.source << "\"},\n";
    out << "  \"orders\": {\"orders_placed\": " << order_metrics.orders_placed
        << ", \"orders_cancelled\": " << order_metrics.orders_cancelled
        << ", \"orders_cancel_noop\": " << order_metrics.orders_cancel_noop
        << ", \"orders_replaced\": " << order_metrics.orders_replaced
        << ", \"orders_replace_noop\": " << order_metrics.orders_replace_noop
        << ", \"orders_rejected\": " << order_metrics.orders_rejected
        << ", \"orders_expired\": " << order_metrics.orders_expired
        << ", \"illegal_transitions\": " << order_metrics.illegal_transitions
        << ", \"open_orders_peak\": " << order_metrics.open_orders_peak << ", \"avg_order_lifetime_ms\": "
        << avg_lifetime_ms << "},\n";
    out << "  \"latency\": {\"base_ms\": " << lat_cfg.base_ms << ", \"jitter_ms\": " << lat_cfg.jitter_ms
        << ", \"tail_ms\": " << lat_cfg.tail_ms << ", \"tail_prob\": " << lat_cfg.tail_prob
        << ", \"source\": \"" << lat_cfg.source << "\""
        << ", \"samples\": {\"p50\": " << lat_p50 << ", \"p90\": " << lat_p90 << ", \"p99\": " << lat_p99
        << ", \"n\": " << lat_n << "}}\n";
    out << "}\n";
    return true;
}

bool write_latency_samples_csv(const std::filesystem::path &path, const std::vector<double> &samples) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << "latency_ms\n";
    for (double v : samples) {
        out << v << "\n";
    }
    return true;
}
}  // namespace

int main(int argc, char **argv) {
    std::string replay_source = "data/replay/synthetic.csv";
    bool no_actions = false;
    double demo_notional = 0.0;
    int64_t demo_interval_ms = 500;
    int demo_max_actions = 30;
    bool demo_only = false;
    bool maker_demo = false;
    double maker_notional = 0.0;
    int64_t maker_interval_ms = 500;
    int maker_max_actions = 30;
    int64_t maker_ttl_ms = 200;
    int64_t adv_horizon_ms = 100;
    bool adv_fatal_missing = true;
    std::string replay_bookcheck_path;
    std::size_t replay_bookcheck_every = 0;
    std::string run_id_override;
    std::string rules_config_path = "config/venue_rules.yaml";
    std::string trades_path;
    std::string latency_fit_path;
    std::string venue = "BYBIT";
    std::string venue_symbol = "BTCUSDT";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no_actions") {
            no_actions = true;
        } else if (arg == "--demo_notional" && i + 1 < argc) {
            demo_notional = std::stod(argv[++i]);
        } else if (arg == "--demo_interval_ms" && i + 1 < argc) {
            demo_interval_ms = std::stoll(argv[++i]);
        } else if (arg == "--demo_max" && i + 1 < argc) {
            demo_max_actions = std::stoi(argv[++i]);
        } else if (arg == "--demo_only") {
            demo_only = true;
        } else if (arg == "--maker_demo") {
            maker_demo = true;
        } else if (arg == "--maker_notional" && i + 1 < argc) {
            maker_notional = std::stod(argv[++i]);
        } else if (arg == "--maker_interval_ms" && i + 1 < argc) {
            maker_interval_ms = std::stoll(argv[++i]);
        } else if (arg == "--maker_max" && i + 1 < argc) {
            maker_max_actions = std::stoi(argv[++i]);
        } else if (arg == "--maker_ttl_ms" && i + 1 < argc) {
            maker_ttl_ms = std::stoll(argv[++i]);
        } else if (arg == "--adv_horizon_ms" && i + 1 < argc) {
            adv_horizon_ms = std::stoll(argv[++i]);
        } else if (arg == "--adv_fatal_missing" && i + 1 < argc) {
            const int flag = std::stoi(argv[++i]);
            adv_fatal_missing = (flag != 0);
        } else if (arg == "--bookcheck" && i + 1 < argc) {
            replay_bookcheck_path = argv[++i];
        } else if (arg == "--bookcheck_every" && i + 1 < argc) {
            replay_bookcheck_every = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--run_id" && i + 1 < argc) {
            run_id_override = argv[++i];
        } else if (arg == "--rules_config" && i + 1 < argc) {
            rules_config_path = argv[++i];
        } else if (arg == "--venue" && i + 1 < argc) {
            venue = argv[++i];
        } else if (arg == "--symbol" && i + 1 < argc) {
            venue_symbol = argv[++i];
        } else if (arg == "--trades" && i + 1 < argc) {
            trades_path = argv[++i];
        } else if (arg == "--latency_fit" && i + 1 < argc) {
            latency_fit_path = argv[++i];
        } else if (replay_source == "data/replay/synthetic.csv") {
            replay_source = arg;
        } else {
            // ignore unknown extras for now
        }
    }

    const std::string run_id = generate_run_id(run_id_override);
    const std::filesystem::path run_dir = std::filesystem::path("runs") / run_id;
    std::error_code dir_ec;
    std::filesystem::create_directories(run_dir, dir_ec);
    if (dir_ec) {
        utils::error("Failed to create run directory: " + run_dir.string() + " err=" + dir_ec.message());
        return 1;
    }
    const std::filesystem::path fills_path = run_dir / "fills.csv";
    const std::filesystem::path metrics_path = run_dir / "metrics.json";
    const std::filesystem::path latency_samples_path = run_dir / "latency_samples.csv";

    engine::RulesConfig rules_cfg{0.1, 0.001, 0.001, 5.0, 0.0, "default"};
    engine::FeeConfig fee_cfg{2.0, 6.0, "USDT", "ceil_to_cent", "default"};
    if (load_config_from_yaml(rules_config_path, venue, venue_symbol, rules_cfg, fee_cfg)) {
        rules_cfg.source = "file:" + rules_config_path;
        fee_cfg.source = "file:" + rules_config_path;
    }
    engine::RulesEngine rules(rules_cfg);
    engine::FeeModel fee_model(fee_cfg);

    engine::EventBus bus(64);
    engine::TickReplay replay;
    replay.load_file(replay_source);
    if (trades_path.empty()) {
        const std::filesystem::path candidate = std::filesystem::path("data/replay/bybit_trades.csv");
        if (std::filesystem::exists(candidate)) {
            trades_path = candidate.string();
        }
    }
    if (!trades_path.empty()) {
        replay.load_trades_file(trades_path);
    }
    if (!replay_bookcheck_path.empty() && replay_bookcheck_every > 0) {
        replay.enable_bookcheck(replay_bookcheck_path, replay_bookcheck_every);
    }

    engine::FeatureEngine feature_engine;
    engine::DecisionEngine decision_engine;
    engine::RiskEngine risk_engine(5.0, 250000.0);
    const double tick_size = 0.1;  // Bybit BTC tick size
    const std::string symbol = "SIM";
    engine::MatchingEngine matching_engine(symbol, tick_size);
    engine::OrderManager order_manager;
    engine::Recorder recorder("engine_events.log");
    engine::TradeTape tape{100.0, 1.0};
    engine::LatencyConfig latency_cfg;
    if (!latency_fit_path.empty()) {
        if (!load_latency_fit(latency_fit_path, latency_cfg)) {
            utils::warn("Failed to load latency fit from " + latency_fit_path + ", using defaults");
        }
    } else if (std::filesystem::exists("config/latency_fit.json")) {
        load_latency_fit("config/latency_fit.json", latency_cfg);
    }
    uint64_t action_seq = 0;
    std::priority_queue<PendingAction, std::vector<PendingAction>, PendingComparator> pending_actions;
    PnLAggregate pnl;
    engine::MakerParams maker_params{};
    if (maker_ttl_ms > 0) {
        maker_params.expire_ms = maker_ttl_ms;
    }
    if (maker_demo) {
        maker_params.q_init = 0.0;
        maker_params.alpha = 1.0;
    }
    engine::MakerQueueSim maker_sim(maker_params, tick_size);
    std::unordered_set<uint64_t> cancelled_orders;
    std::unordered_set<uint64_t> maker_open_orders;
    std::unordered_set<uint64_t> pending_order_ids;
    std::vector<PendingMakerAdv> pending_maker_adv;
    int64_t last_demo_ts = 0;
    int demo_sent = 0;
    int64_t last_maker_demo_ts = 0;
    int maker_demo_sent = 0;
    std::vector<FillRow> fill_rows;
    const bool demo_mode = demo_notional > 0.0;
    if (demo_mode || maker_demo) {
        no_actions = false;  // demo overrides global no_actions intent
    }
    auto close_order_tracking = [&](uint64_t order_id) {
        auto it = order_manager.orders().find(order_id);
        if (it == order_manager.orders().end()) {
            return;
        }
        const auto st = it->second.status;
        if (st == engine::OrderStatus::Filled || st == engine::OrderStatus::Cancelled ||
            st == engine::OrderStatus::Expired || st == engine::OrderStatus::Replaced ||
            st == engine::OrderStatus::Rejected) {
            maker_open_orders.erase(order_id);
            pending_order_ids.erase(order_id);
            cancelled_orders.erase(order_id);
        }
    };

    transport::ZmqServer feature_pub("tcp://*:7001");
    transport::GrpcServer action_pub("0.0.0.0:50051");
    feature_pub.start();
    action_pub.start();

    auto handle_reject = [&](const engine::Fill &fill, bool demo, double target_notional, const std::string &src) {
        const auto &book = replay.current_book();
        const double mid =
            (book.best_bid > 0.0 && book.best_ask > 0.0) ? (book.best_bid + book.best_ask) / 2.0 : 0.0;
        const double best = (fill.side == engine::Side::Buy) ? book.best_ask : book.best_bid;
        pnl.rejects_total += 1;
        pnl.reject_counts[reason_str(fill.reason)] += 1;
        FillRow row;
        row.ts_ms = book.ts_ms;
        row.seq = replay.current_seq();
        row.status = status_str(fill.status);
        row.side = side_str(fill.side);
        row.liquidity = "NONE";
        row.src = src;
        row.reason = reason_str(fill.reason);
        row.mid = mid;
        row.best = best;
        row.target_notional = target_notional;
        row.order_id = fill.order_id;
        fill_rows.push_back(row);
        std::stringstream ss;
        ss << "fill_reject side=" << static_cast<int>(fill.side) << " reason=" << static_cast<int>(fill.reason);
        bus.publish(engine::Event{engine::Event::Type::Fill, ss.str()});
    };

    auto handle_fill = [&](const engine::Fill &fill, bool demo, bool crossing, double target_notional,
                           const std::string &src, double adv_ticks) {
        const auto &book = replay.current_book();
        const double mid =
            (book.best_bid > 0.0 && book.best_ask > 0.0) ? (book.best_bid + book.best_ask) / 2.0 : fill.vwap_price;
        const double best = (fill.side == engine::Side::Buy) ? book.best_ask : book.best_bid;

        double mark = mid;
        if (mark <= 0.0) {
            mark = fill.vwap_price;
        }
        const double prev_mark_pnl = risk_engine.position().pnl +
                                     risk_engine.position().qty * (mark - risk_engine.position().avg_price);
        risk_engine.update(fill);
        const double new_mark_pnl = risk_engine.position().pnl +
                                    risk_engine.position().qty * (mark - risk_engine.position().avg_price);
        const double gross_delta = new_mark_pnl - prev_mark_pnl;
        const auto fee_res = fee_model.compute(fill);
        const double notional = fill.vwap_price * fill.filled_qty;
        const double fee_paid = fee_res.fee;
        const double fee_bps = fee_res.fee_bps;
        pnl.turnover += std::abs(notional);
        pnl.gross += gross_delta;
        pnl.fees += fee_paid;
        const double cfg_fee_bps =
            (fill.liquidity == engine::Liquidity::Maker) ? fee_model.config().maker_bps : fee_model.config().taker_bps;
        pnl.fee_bps_samples.push_back(cfg_fee_bps);
        if (fill.liquidity == engine::Liquidity::Maker) {
            pnl.fee_bps_maker_samples.push_back(cfg_fee_bps);
        } else {
            pnl.fee_bps_taker_samples.push_back(cfg_fee_bps);
        }
        const double net_delta = gross_delta - fee_paid;
        pnl.net_steps.push_back(net_delta);
        const double spread_paid_ticks = (mid > 0.0) ? std::abs(fill.vwap_price - mid) / tick_size : 0.0;
        const double exec_cost_ticks_signed =
            (mid > 0.0) ? ((fill.side == engine::Side::Buy) ? (fill.vwap_price - mid) / tick_size
                                                            : (mid - fill.vwap_price) / tick_size)
                        : 0.0;
        pnl.exec_cost_ticks_signed_samples.push_back(exec_cost_ticks_signed);
        if (fill.liquidity == engine::Liquidity::Maker) {
            pnl.exec_cost_ticks_signed_maker_samples.push_back(exec_cost_ticks_signed);
        } else {
            pnl.exec_cost_ticks_signed_taker_samples.push_back(exec_cost_ticks_signed);
        }
        if (target_notional > 0.0 && notional > 0.0) {
            pnl.filled_to_target_samples.push_back(notional / target_notional);
        }
        const double mid_to_best_ticks = (mid > 0.0 && best > 0.0) ? (mid - best) / tick_size : 0.0;
        const int64_t bucket_1s = book.ts_ms / 1000;
        const int64_t bucket_10s = book.ts_ms / 10000;
        pnl.net_by_1s[bucket_1s] += net_delta;
        pnl.net_by_10s[bucket_10s] += net_delta;
        pnl.fills_total += 1;
        if (fill.liquidity == engine::Liquidity::Maker) {
            pnl.maker_fills += 1;
        } else {
            pnl.taker_fills += 1;
        }

        FillRow row;
        row.ts_ms = book.ts_ms;
        row.seq = replay.current_seq();
        row.status = status_str(fill.status);
        row.side = side_str(fill.side);
        row.liquidity = liquidity_str(fill.liquidity);
        row.src = src;
        row.reason = reason_str(fill.reason);
        row.order_id = fill.order_id;
        row.vwap = fill.vwap_price;
        row.filled_qty = fill.filled_qty;
        row.unfilled_qty = fill.unfilled_qty;
        row.fee = fee_paid;
        row.fee_bps = fee_bps;
        row.gross = gross_delta;
        row.net = net_delta;
        row.exec_cost_ticks_signed = exec_cost_ticks_signed;
        row.mid = mid;
        row.best = best;
        row.spread_paid_ticks = spread_paid_ticks;
        row.slip_ticks = fill.slippage_ticks;
        row.target_notional = target_notional;
        row.filled_notional = notional;
        row.crossing = crossing ? 1 : 0;
        row.levels_crossed = static_cast<int>(fill.levels_crossed);
        row.adv_ticks = adv_ticks;
        row.queue_time_ms = 0.0;
        row.adv_selection_ticks = 0.0;
        std::size_t row_idx = fill_rows.size();
        fill_rows.push_back(row);
        if (fill.liquidity == engine::Liquidity::Maker) {
            auto it = order_manager.orders().find(fill.order_id);
            if (it != order_manager.orders().end()) {
                const double qt = static_cast<double>(book.ts_ms - it->second.created_ts);
                fill_rows[row_idx].queue_time_ms = qt;
                pnl.maker_queue_times_ms.push_back(qt);
            }
            if (mid > 0.0) {
                pending_maker_adv.push_back(
                    PendingMakerAdv{mid, fill.vwap_price, fill.side, row_idx, book.ts_ms + adv_horizon_ms});
            }
        }

        std::stringstream ss;
        ss << "fill side=" << static_cast<int>(fill.side) << " vwap=" << fill.vwap_price << " filled="
           << fill.filled_qty << " unfilled=" << fill.unfilled_qty << " levels=" << fill.levels_crossed
           << " slip_ticks=" << fill.slippage_ticks << " partial=" << (fill.partial ? 1 : 0)
           << " spread_paid_ticks=" << spread_paid_ticks << " liq="
           << (fill.liquidity == engine::Liquidity::Maker ? "M" : "T") << " src=" << src
           << " target_notional=" << target_notional << " filled_notional=" << notional
           << " crossing=" << (crossing ? 1 : 0) << " best=" << best << " mid=" << mid
           << " mid_to_best_ticks=" << mid_to_best_ticks << " exec_cost_ticks_signed=" << exec_cost_ticks_signed
           << " adv_ticks=" << adv_ticks << " fee=" << fee_paid << " fee_bps=" << fee_bps << " gross=" << gross_delta
           << " net=" << net_delta << " fees_tot=" << pnl.fees << " net_tot=" << pnl.net();
        bus.publish(engine::Event{engine::Event::Type::Fill, ss.str()});
        utils::info(ss.str());
    };
    while (replay.feed_next(bus)) {
        const double current_mid =
            (replay.current_book().best_bid > 0.0 && replay.current_book().best_ask > 0.0)
                ? (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0
                : 0.0;
        if (current_mid > 0.0 && !pending_maker_adv.empty()) {
            std::vector<PendingMakerAdv> remaining;
            for (const auto &p : pending_maker_adv) {
                if (replay.current_book().ts_ms >= p.target_ts_ms) {
                    const double delta_mid = current_mid - p.mid_at_fill;
                    const double adv = (p.side == engine::Side::Buy ? delta_mid : -delta_mid) / tick_size;
                    pnl.maker_adv_ticks.push_back(adv);
                    if (p.fill_row_index < fill_rows.size()) {
                        fill_rows[p.fill_row_index].adv_selection_ticks = adv;
                    }
                } else {
                    remaining.push_back(p);
                }
            }
            pending_maker_adv.swap(remaining);
        }
        auto evt = bus.poll();
        if (!evt) {
            continue;
        }
        recorder.record(*evt);

        // expire open orders based on replay time
        order_manager.expire_orders(replay.current_book().ts_ms);
        for (const auto &kv : order_manager.orders()) {
            if (kv.second.status == engine::OrderStatus::Expired) {
                maker_sim.cancel(kv.first);
                close_order_tracking(kv.first);
            }
        }

        // Update maker queue fills against the latest book.
        const int64_t now_ts = replay.current_book().ts_ms;
        const auto trades = replay.drain_trades_up_to(now_ts);
        for (const auto &tp : trades) {
            pnl.trade_skews_ms.push_back(static_cast<double>(now_ts - tp.ts_ms));
        }
        if (!trades.empty()) {
            tape.last_price = trades.back().price;
            tape.last_size = trades.back().size;
        }
        if (!no_actions) {
            for (auto &fill : maker_sim.on_book(replay.current_book(), now_ts, trades)) {
                if (fill.status == engine::FillStatus::Filled) {
                    handle_fill(fill, false, false, 0.0, "MAKER", maker_params.adv_ticks);
                    order_manager.apply_fill(fill, now_ts);
                    if (order_manager.has_error()) {
                        utils::error("[FATAL][orders] " + order_manager.error_message());
                        return 1;
                    }
                    close_order_tracking(fill.order_id);
                }
            }
        }

        // Process pending actions whose fill_ts <= current book ts.
        // (use same now_ts)
        if (!no_actions) {
            while (!pending_actions.empty() && pending_actions.top().fill_ts <= now_ts) {
                const auto pending = pending_actions.top();
                pending_actions.pop();
                if (cancelled_orders.count(pending.order_id)) {
                    pending_order_ids.erase(pending.order_id);
                    continue;
                }
                auto ord_it = order_manager.orders().find(pending.order_id);
                if (ord_it != order_manager.orders().end()) {
                    const auto st = ord_it->second.status;
                    if (st == engine::OrderStatus::Cancelled || st == engine::OrderStatus::Expired ||
                        st == engine::OrderStatus::Replaced || st == engine::OrderStatus::Filled ||
                        st == engine::OrderStatus::Rejected) {
                        pending_order_ids.erase(pending.order_id);
                        continue;
                    }
                }
                auto fill = matching_engine.simulate(pending.action, replay.current_book());
                const std::string src = pending.demo ? "DEMO" : "STRAT";
                if (fill.status == engine::FillStatus::Filled) {
                    const double target_notional = pending.target_notional;
                    const double notional = fill.vwap_price * fill.filled_qty;
                    if (target_notional > 0.0 && notional > target_notional * 1.001) {
                        std::stringstream err;
                        err << "[fee_sanity] filled_notional " << notional << " exceeds target " << target_notional;
                        utils::error(err.str());
                        std::exit(1);
                    }
                    fill.order_id = pending.order_id;
                    handle_fill(fill, pending.demo, pending.crossing, target_notional, src,
                                fill.liquidity == engine::Liquidity::Maker ? maker_params.adv_ticks : 0.0);
                    action_pub.publish({pending.action, symbol});
                    order_manager.apply_fill(fill, now_ts);
                    if (order_manager.has_error()) {
                        utils::error("[FATAL][orders] " + order_manager.error_message());
                        return 1;
                    }
                    close_order_tracking(fill.order_id);
                } else {
                    handle_reject(fill, pending.demo, pending.target_notional, src);
                    order_manager.mark_rejected(pending.order_id, now_ts);
                    close_order_tracking(pending.order_id);
                }
                pending_order_ids.erase(pending.order_id);
            }
        }

        auto feature = feature_engine.compute(replay.current_book(), tape);
        feature_pub.publish({feature, "SIM"});

        if (!no_actions) {
            if (demo_mode && demo_sent >= demo_max_actions) {
                if (demo_only && !maker_demo) {
                    continue;  // cap demo actions; still consume ticks
                }
            }
            bool issued_demo = false;
            engine::Action action;
            // Maker demo generation
            if (maker_demo && maker_demo_sent < maker_max_actions) {
                if (last_maker_demo_ts == 0 || (now_ts - last_maker_demo_ts) >= maker_interval_ms) {
                    double ref_px_buy = replay.current_book().best_bid;
                    double ref_px_sell = replay.current_book().best_ask;
                    if (ref_px_buy > 0.0 && ref_px_sell > 0.0) {
                        const bool do_buy = (maker_demo_sent % 2 == 0);
                        action.side = do_buy ? engine::Side::Buy : engine::Side::Sell;
                        const double ref_px = do_buy ? ref_px_buy : ref_px_sell;
                        const double qty = (maker_notional > 0.0 && ref_px > 0.0) ? (maker_notional / ref_px) : 0.0;
                        if (qty > 0.0) {
                            action.size = qty;
                            action.notional = maker_notional;
                            action.is_maker = true;
                            action.limit_price = do_buy ? ref_px_buy : ref_px_sell;
                            action.type = engine::OrderType::Limit;
                            last_maker_demo_ts = now_ts;
                            issued_demo = true;
                            ++maker_demo_sent;
                        }
                    }
                }
            }

            if (demo_notional > 0.0 && demo_sent < demo_max_actions) {
                if (last_demo_ts == 0 || (now_ts - last_demo_ts) >= demo_interval_ms) {
                    double ref_px = replay.current_book().best_ask;
                    if (ref_px <= 0.0) {
                        ref_px = replay.current_book().best_bid;
                    }
                    if (ref_px <= 0.0 && replay.current_book().best_bid > 0.0 &&
                        replay.current_book().best_ask > 0.0) {
                        ref_px = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
                    }
                    if (ref_px <= 0.0) {
                        if (demo_only && issued_demo) {
                            // keep maker demo
                        } else {
                            // no valid ref price for taker; skip this tick
                            continue;
                        }
                    }
                    if (!issued_demo) {
                        const double qty = demo_notional / ref_px;
                        if (qty > 0.0) {
                            last_demo_ts = now_ts;
                            action.side = engine::Side::Buy;
                            action.size = qty;
                            action.notional = demo_notional;
                            action.is_maker = false;
                            issued_demo = true;
                        }
                    }
                }
            }
            if (demo_notional > 0.0 && !issued_demo) {
                // demo mode: skip other actions when not time yet
                if (demo_only && !maker_demo) {
                    continue;
                }
            }
            if (demo_only && !issued_demo) {
                continue;
            }
            if (!issued_demo) {
                action = decision_engine.decide(feature);
            }
            pnl.actions_attempted += 1;
            if (action.kind == engine::ActionKind::Cancel) {
                const uint64_t oid = action.target_order_id;
                auto res = order_manager.cancel(oid, now_ts);
                if (res.success) {
                    cancelled_orders.insert(oid);
                    maker_sim.cancel(oid);
                    pending_order_ids.erase(oid);
                    close_order_tracking(oid);
                }
                continue;
            }
            if (action.kind == engine::ActionKind::Replace) {
                const uint64_t oid = action.target_order_id;
                auto ord_it = order_manager.orders().find(oid);
                if (ord_it == order_manager.orders().end()) {
                    continue;
                }
                const auto &old = ord_it->second;
                engine::Action replace_action = action;
                replace_action.kind = engine::ActionKind::Place;
                replace_action.side = old.side;
                replace_action.type = old.type;
                if (replace_action.size <= 0.0) {
                    replace_action.size = std::max(0.0, old.qty - old.filled_qty);
                }
                if (replace_action.limit_price <= 0.0) {
                    replace_action.limit_price = old.price;
                }
                const auto rules_res = rules.apply(replace_action, replay.current_book());
                if (!rules_res.ok) {
                    auto rej = engine::Fill::rejected(replace_action.side, rules_res.reason);
                    rej.order_id = oid;
                    handle_reject(rej, issued_demo, replace_action.notional, issued_demo ? "DEMO" : "STRAT");
                    order_manager.mark_rejected(oid, now_ts);
                    close_order_tracking(oid);
                    continue;
                }
                replace_action = rules_res.normalized;
                const double ref_price =
                    (replace_action.side == engine::Side::Buy) ? replay.current_book().best_ask : replay.current_book().best_bid;
                const double last_px = (ref_price > 0.0) ? ref_price : replace_action.limit_price;
                if (!risk_engine.validate(replace_action, last_px)) {
                    auto rej = engine::Fill::rejected(replace_action.side, engine::RejectReason::RiskLimit);
                    rej.order_id = oid;
                    handle_reject(rej, issued_demo, replace_action.notional, issued_demo ? "DEMO" : "STRAT");
                    order_manager.mark_rejected(oid, now_ts);
                    close_order_tracking(oid);
                    continue;
                }
                const bool new_is_maker = action.is_maker || maker_open_orders.count(oid) > 0;
                bool crossing_replace = engine::is_crossing_limit(replace_action, replay.current_book());
                const bool final_is_maker = new_is_maker && !crossing_replace;
                auto rep_res = order_manager.replace(oid, replace_action.limit_price, replace_action.size, now_ts,
                                                     now_ts + static_cast<int64_t>(maker_params.expire_ms));
                if (!rep_res.success) {
                    continue;
                }
                cancelled_orders.insert(oid);
                maker_sim.cancel(oid);
                pending_order_ids.erase(oid);
                close_order_tracking(oid);
                replace_action.order_id = rep_res.new_order.order_id;
                replace_action.is_maker = final_is_maker;
                if (replace_action.is_maker) {
                    maker_sim.submit(replace_action, replay.current_book(), now_ts);
                    maker_open_orders.insert(replace_action.order_id);
                    pnl.maker_orders_submitted += 1;
                } else {
                    const double latency_ms =
                        engine::deterministic_latency_ms(symbol, action_seq, action_seq, latency_cfg);
                    const int64_t fill_ts = now_ts + static_cast<int64_t>(latency_ms);
                    pending_actions.push(PendingAction{replace_action, fill_ts, action_seq, action_seq, issued_demo,
                                                       replace_action.notional, crossing_replace,
                                                       replace_action.order_id});
                    pnl.latency_samples_ms.push_back(latency_ms);
                    pending_order_ids.insert(replace_action.order_id);
                    ++action_seq;
                }
                continue;
            }
            const auto rules_res = rules.apply(action, replay.current_book());
            if (!rules_res.ok) {
                auto rej = engine::Fill::rejected(action.side, rules_res.reason);
                handle_reject(rej, issued_demo, action.notional, issued_demo ? "DEMO" : "STRAT");
                continue;
            }
            action = rules_res.normalized;
            const double ref_price =
                (action.side == engine::Side::Buy) ? replay.current_book().best_ask : replay.current_book().best_bid;
            const double last_px = (ref_price > 0.0) ? ref_price : action.limit_price;
            if (!risk_engine.validate(action, last_px)) {
                auto rej = engine::Fill::rejected(action.side, engine::RejectReason::RiskLimit);
                handle_reject(rej, issued_demo, action.notional, issued_demo ? "DEMO" : "STRAT");
                continue;
            }

            const bool crossing = engine::is_crossing_limit(action, replay.current_book());
            if (crossing) {
                action.is_maker = false;
            }
            // place order and attach id
            auto placed = order_manager.place(action, now_ts, now_ts + static_cast<int64_t>(maker_params.expire_ms));
            action.order_id = placed.order_id;
            if (action.is_maker) {
                maker_sim.submit(action, replay.current_book(), now_ts);
                maker_open_orders.insert(action.order_id);
                pnl.maker_orders_submitted += 1;
            } else {
                const double latency_ms = engine::deterministic_latency_ms(symbol, action_seq, action_seq, latency_cfg);
                const int64_t fill_ts = now_ts + static_cast<int64_t>(latency_ms);
                pending_actions.push(
                    PendingAction{action, fill_ts, action_seq, action_seq, issued_demo, action.notional, crossing,
                                  placed.order_id});
                pnl.latency_samples_ms.push_back(latency_ms);
                pending_order_ids.insert(action.order_id);
                ++action_seq;
                if (issued_demo) {
                    ++demo_sent;
                }
            }
        }
    }

    if (replay.has_error()) {
        utils::error("[FATAL] " + replay.last_error());
        return 1;
    }

    // Flush any remaining pending actions against last known book state.
    if (!no_actions) {
        while (!pending_actions.empty()) {
            const auto pending = pending_actions.top();
            pending_actions.pop();
            if (cancelled_orders.count(pending.order_id)) {
                pending_order_ids.erase(pending.order_id);
                continue;
            }
            auto ord_it = order_manager.orders().find(pending.order_id);
            if (ord_it != order_manager.orders().end()) {
                const auto st = ord_it->second.status;
                if (st == engine::OrderStatus::Cancelled || st == engine::OrderStatus::Expired ||
                    st == engine::OrderStatus::Replaced || st == engine::OrderStatus::Filled ||
                    st == engine::OrderStatus::Rejected) {
                    pending_order_ids.erase(pending.order_id);
                    continue;
                }
            }
            auto fill = matching_engine.simulate(pending.action, replay.current_book());
            const std::string src = pending.demo ? "DEMO" : "STRAT";
            if (fill.status == engine::FillStatus::Filled) {
                const double target_notional = pending.target_notional;
                const double notional = fill.vwap_price * fill.filled_qty;
                if (target_notional > 0.0 && notional > target_notional * 1.001) {
                    std::stringstream err;
                    err << "[fee_sanity] filled_notional " << notional << " exceeds target " << target_notional;
                    utils::error(err.str());
                    std::exit(1);
                }
                handle_fill(fill, pending.demo, pending.crossing, target_notional, src,
                            fill.liquidity == engine::Liquidity::Maker ? maker_params.adv_ticks : 0.0);
                order_manager.apply_fill(fill, replay.current_book().ts_ms);
                if (order_manager.has_error()) {
                    utils::error("[FATAL][orders] " + order_manager.error_message());
                    return 1;
                }
                close_order_tracking(fill.order_id);
            } else {
                handle_reject(fill, pending.demo, pending.target_notional, src);
                order_manager.mark_rejected(pending.order_id, replay.current_book().ts_ms);
                close_order_tracking(pending.order_id);
            }
            pending_order_ids.erase(pending.order_id);
        }
    }

    if (!pending_maker_adv.empty()) {
        const std::string msg = "[FATAL] maker adv_selection horizon not reached for " +
                                std::to_string(pending_maker_adv.size()) + " fills";
        if (adv_fatal_missing) {
            utils::error(msg);
            return 1;
        }
        utils::warn(msg);
    }

    if (order_manager.has_error()) {
        utils::error("[FATAL][orders] " + order_manager.error_message());
        return 1;
    }

    std::stringstream summary;
    const double final_mid =
        (replay.current_book().best_bid + replay.current_book().best_ask) > 0
            ? (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0
            : risk_engine.position().avg_price;
    const double unrealized = risk_engine.position().qty * (final_mid - risk_engine.position().avg_price);
    const double realized = risk_engine.realized_pnl();
    const double net_total = realized + unrealized - pnl.fees;
    summary << "PnL realized=" << realized << " unrealized=" << unrealized << " fees=" << pnl.fees
            << " net_total=" << net_total << " gross_mark=" << pnl.gross;
    if (std::abs(pnl.gross) > 1e-9) {
        summary << " fee_ratio=" << (pnl.fees / pnl.gross);
    }
    auto s1 = PnLAggregate::sharpe_from_buckets(pnl.net_by_1s);
    auto s10 = PnLAggregate::sharpe_from_buckets(pnl.net_by_10s);
    summary << " net_sharpe_1s=" << s1.sharpe << " n1s=" << s1.n << " std1s=" << s1.std
            << " net_sharpe_10s=" << s10.sharpe << " n10s=" << s10.n << " std10s=" << s10.std;
    utils::info(summary.str());

    const double identity_lhs = realized + unrealized - pnl.fees;
    const bool identity_ok =
        std::isfinite(identity_lhs) && std::isfinite(net_total) && std::abs(identity_lhs - net_total) <= 1e-6;
    const double max_dd = pnl.max_drawdown();
    const double fill_rate = pnl.fill_rate();
    const double maker_fill_rate = pnl.maker_fill_rate();
    auto mean_vec = [](const std::vector<double> &v) -> double {
        if (v.empty()) {
            return 0.0;
        }
        double s = 0.0;
        for (double x : v) {
            s += x;
        }
        return s / static_cast<double>(v.size());
    };
    const double maker_queue_avg = mean_vec(pnl.maker_queue_times_ms);
    const double maker_queue_p90 = percentile(pnl.maker_queue_times_ms, 90.0);
    const double maker_adv_mean = mean_vec(pnl.maker_adv_ticks);
    const double maker_adv_p90 = percentile(pnl.maker_adv_ticks, 90.0);
    const std::size_t maker_adv_count = pnl.maker_adv_ticks.size();
    const double lat_p50 = percentile(pnl.latency_samples_ms, 50.0);
    const double lat_p90 = percentile(pnl.latency_samples_ms, 90.0);
    const double lat_p99 = percentile(pnl.latency_samples_ms, 99.0);
    const std::size_t lat_n = pnl.latency_samples_ms.size();
    const double trade_skew_p50 = percentile(pnl.trade_skews_ms, 50.0);
    const double trade_skew_p90 = percentile(pnl.trade_skews_ms, 90.0);
    const double trade_skew_p99 = percentile(pnl.trade_skews_ms, 99.0);
    const std::size_t trade_skew_n = pnl.trade_skews_ms.size();
    const double fee_bps_p50 = percentile(pnl.fee_bps_samples, 50.0);
    const double fee_bps_p99 = percentile(pnl.fee_bps_samples, 99.0);
    const double fee_bps_maker_p50 = percentile(pnl.fee_bps_maker_samples, 50.0);
    const double fee_bps_maker_p90 = percentile(pnl.fee_bps_maker_samples, 90.0);
    const double fee_bps_maker_p99 = percentile(pnl.fee_bps_maker_samples, 99.0);
    const double fee_bps_taker_p50 = percentile(pnl.fee_bps_taker_samples, 50.0);
    const double fee_bps_taker_p90 = percentile(pnl.fee_bps_taker_samples, 90.0);
    const double fee_bps_taker_p99 = percentile(pnl.fee_bps_taker_samples, 99.0);
    const double exec_cost_p50 = percentile(pnl.exec_cost_ticks_signed_samples, 50.0);
    const double exec_cost_p99 = percentile(pnl.exec_cost_ticks_signed_samples, 99.0);
    const double exec_cost_std = sample_stddev(pnl.exec_cost_ticks_signed_samples);
    const double exec_cost_maker_p50 = percentile(pnl.exec_cost_ticks_signed_maker_samples, 50.0);
    const double exec_cost_maker_p99 = percentile(pnl.exec_cost_ticks_signed_maker_samples, 99.0);
    const double exec_cost_maker_std = sample_stddev(pnl.exec_cost_ticks_signed_maker_samples);
    const double exec_cost_taker_p50 = percentile(pnl.exec_cost_ticks_signed_taker_samples, 50.0);
    const double exec_cost_taker_p99 = percentile(pnl.exec_cost_ticks_signed_taker_samples, 99.0);
    const double exec_cost_taker_std = sample_stddev(pnl.exec_cost_ticks_signed_taker_samples);
    const double filled_to_target_p99 = percentile(pnl.filled_to_target_samples, 99.0);

    if (!write_fills_csv(fills_path, fill_rows)) {
        utils::error("Failed to write fills CSV to " + fills_path.string());
        return 1;
    }
    if (!write_latency_samples_csv(latency_samples_path, pnl.latency_samples_ms)) {
        utils::warn("Failed to write latency samples to " + latency_samples_path.string());
    }
    const auto &ord_metrics = order_manager.metrics();
    const double avg_lifetime_ms =
        (ord_metrics.lifetime_samples > 0) ? (ord_metrics.total_lifetime_ms / ord_metrics.lifetime_samples) : 0.0;

    if (!write_metrics_json(metrics_path, run_id, pnl, realized, unrealized, net_total, s1, s10, max_dd, fill_rate,
                            maker_fill_rate, maker_queue_avg, maker_queue_p90, maker_adv_mean, maker_adv_p90,
                            pnl.reject_counts, identity_ok, rules_cfg, fee_cfg, ord_metrics, avg_lifetime_ms, latency_cfg,
                            lat_p50, lat_p90, lat_p99, lat_n, trade_skew_p50, trade_skew_p90, trade_skew_p99,
                            trade_skew_n, maker_adv_count, fee_bps_p50, fee_bps_p99, exec_cost_p50, exec_cost_p99,
                            exec_cost_std, filled_to_target_p99, fee_bps_maker_p50, fee_bps_maker_p90,
                            fee_bps_maker_p99, fee_bps_taker_p50, fee_bps_taker_p90, fee_bps_taker_p99,
                            exec_cost_maker_p50, exec_cost_maker_p99, exec_cost_maker_std, exec_cost_taker_p50,
                            exec_cost_taker_p99, exec_cost_taker_std)) {
        utils::error("Failed to write metrics JSON to " + metrics_path.string());
        return 1;
    }
    utils::info("Structured outputs written to " + fills_path.string() + " and " + metrics_path.string());

    if (!identity_ok) {
        utils::error("[FATAL] identity check failed: net_total != realized + unrealized - fees");
        return 1;
    }

    recorder.flush();
    feature_pub.stop();
    action_pub.stop();
    utils::info("Engine run complete.");
    return 0;
}
