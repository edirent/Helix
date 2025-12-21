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
#include <iomanip>

#include "engine/decision_engine.hpp"
#include "engine/latency.hpp"
#include "engine/event_bus.hpp"
#include "engine/feature_engine.hpp"
#include "engine/matching_engine.hpp"
#include "engine/order_utils.hpp"
#include "engine/maker_queue.hpp"
#include "engine/recorder.hpp"
#include "engine/risk_engine.hpp"
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
};

struct PnLAggregate {
    double gross{0.0};
    double fees{0.0};
    std::vector<double> net_steps;
    std::map<int64_t, double> net_by_1s;
    std::map<int64_t, double> net_by_10s;
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
};

struct PendingComparator {
    bool operator()(const PendingAction &a, const PendingAction &b) const { return a.fill_ts > b.fill_ts; }
};

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
        default:
            return "Unknown";
    }
}

struct FillRow {
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

bool write_fills_csv(const std::filesystem::path &path, const std::vector<FillRow> &rows) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << "ts_ms,seq,status,side,liquidity,src,reason,vwap,filled_qty,unfilled_qty,fee,fee_bps,gross,net,"
           "exec_cost_ticks_signed,mid,best,spread_paid_ticks,slip_ticks,target_notional,filled_notional,crossing,levels_crossed,adv_ticks\n";
    out << std::setprecision(10);
    for (const auto &r : rows) {
        out << r.ts_ms << "," << r.seq << "," << r.status << "," << r.side << "," << r.liquidity << "," << r.src
            << "," << r.reason << "," << r.vwap << "," << r.filled_qty << "," << r.unfilled_qty << "," << r.fee << ","
            << r.fee_bps << "," << r.gross << "," << r.net << "," << r.exec_cost_ticks_signed << "," << r.mid << ","
            << r.best << "," << r.spread_paid_ticks << "," << r.slip_ticks << "," << r.target_notional << ","
            << r.filled_notional << "," << r.crossing << "," << r.levels_crossed << "," << r.adv_ticks << "\n";
    }
    return true;
}

bool write_metrics_json(const std::filesystem::path &path, const std::string &run_id, const PnLAggregate &pnl,
                        double realized, double unrealized, double net_total, const PnLAggregate::SharpeStats &s1,
                        const PnLAggregate::SharpeStats &s10, double max_dd, double fill_rate,
                        const std::unordered_map<std::string, int> &reject_counts, bool identity_ok) {
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
    out << "  \"fills_total\": " << pnl.fills_total << ",\n";
    out << "  \"makers\": " << pnl.maker_fills << ",\n";
    out << "  \"takers\": " << pnl.taker_fills << ",\n";
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
    out << "  }\n";
    out << "}\n";
    return true;
}
}  // namespace

int main(int argc, char **argv) {
    std::string replay_source = "data/replay/synthetic.csv";
    bool no_actions = false;
    double demo_notional = 0.0;
    int64_t demo_interval_ms = 500;
    int demo_max_actions = 30;
    std::string replay_bookcheck_path;
    std::size_t replay_bookcheck_every = 0;
    std::string run_id_override;
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
        } else if (arg == "--bookcheck" && i + 1 < argc) {
            replay_bookcheck_path = argv[++i];
        } else if (arg == "--bookcheck_every" && i + 1 < argc) {
            replay_bookcheck_every = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--run_id" && i + 1 < argc) {
            run_id_override = argv[++i];
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

    engine::EventBus bus(64);
    engine::TickReplay replay;
    replay.load_file(replay_source);
    if (!replay_bookcheck_path.empty() && replay_bookcheck_every > 0) {
        replay.enable_bookcheck(replay_bookcheck_path, replay_bookcheck_every);
    }

    engine::FeatureEngine feature_engine;
    engine::DecisionEngine decision_engine;
    engine::RiskEngine risk_engine(5.0, 250000.0);
    const double tick_size = 0.1;  // Bybit BTC tick size
    const std::string symbol = "SIM";
    engine::MatchingEngine matching_engine(symbol, tick_size);
    engine::Recorder recorder("engine_events.log");
    engine::TradeTape tape{100.0, 1.0};
    engine::LatencyConfig latency_cfg;
    uint64_t action_seq = 0;
    std::priority_queue<PendingAction, std::vector<PendingAction>, PendingComparator> pending_actions;
    PnLAggregate pnl;
    engine::MakerParams maker_params{};
    engine::MakerQueueSim maker_sim(maker_params, tick_size);
    int64_t last_demo_ts = 0;
    int demo_sent = 0;
    std::vector<FillRow> fill_rows;
    const bool demo_mode = demo_notional > 0.0;
    if (demo_mode) {
        no_actions = false;  // demo overrides global no_actions intent
    }

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
        const double fee_rate = (fill.liquidity == engine::Liquidity::Maker) ? 0.0002 : 0.0006;
        const double notional = fill.vwap_price * fill.filled_qty;
        const double fee_paid = notional * fee_rate;
        const double fee_bps = notional > 0.0 ? (fee_paid / notional) * 1e4 : 0.0;
        pnl.turnover += std::abs(notional);
        pnl.gross += gross_delta;
        pnl.fees += fee_paid;
        const double net_delta = gross_delta - fee_paid;
        pnl.net_steps.push_back(net_delta);
        const double spread_paid_ticks = (mid > 0.0) ? std::abs(fill.vwap_price - mid) / tick_size : 0.0;
        const double exec_cost_ticks_signed =
            (mid > 0.0) ? ((fill.side == engine::Side::Buy) ? (fill.vwap_price - mid) / tick_size
                                                            : (mid - fill.vwap_price) / tick_size)
                        : 0.0;
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
        fill_rows.push_back(row);

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
        auto evt = bus.poll();
        if (!evt) {
            continue;
        }
        recorder.record(*evt);

        // Update maker queue fills against the latest book.
        const int64_t now_ts = replay.current_book().ts_ms;
        if (!no_actions) {
            for (auto &fill : maker_sim.on_book(replay.current_book(), now_ts)) {
                if (fill.status == engine::FillStatus::Filled) {
                    handle_fill(fill, false, false, 0.0, "MAKER", maker_params.adv_ticks);
                }
            }
        }

        // Process pending actions whose fill_ts <= current book ts.
        // (use same now_ts)
        if (!no_actions) {
            while (!pending_actions.empty() && pending_actions.top().fill_ts <= now_ts) {
                const auto pending = pending_actions.top();
                pending_actions.pop();
                auto fill = matching_engine.simulate(pending.action, replay.current_book());
                pnl.actions_attempted += 1;
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
                    action_pub.publish({pending.action, symbol});
                } else {
                    handle_reject(fill, pending.demo, pending.target_notional, src);
                }
            }
        }

        auto feature = feature_engine.compute(replay.current_book(), tape);
        feature_pub.publish({feature, "SIM"});

        if (!no_actions) {
            if (demo_mode && demo_sent >= demo_max_actions) {
                continue;  // cap demo actions; still consume ticks for invariants/pending fills
            }
            bool issued_demo = false;
            engine::Action action;
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
                        continue;  // no valid reference price
                    }
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
            if (demo_notional > 0.0 && !issued_demo) {
                // demo mode: skip other actions when not time yet
                continue;
            }
            if (!issued_demo) {
                action = decision_engine.decide(feature);
            }
            if (risk_engine.validate(action, replay.current_book().best_ask)) {
                const bool crossing = engine::is_crossing_limit(action, replay.current_book());
                if (crossing) {
                    action.is_maker = false;
                }
                if (action.is_maker) {
                    maker_sim.submit(action, replay.current_book(), now_ts);
                } else {
                    const double latency_ms =
                        engine::deterministic_latency_ms(symbol, action_seq, action_seq, latency_cfg);
                    const int64_t fill_ts = now_ts + static_cast<int64_t>(latency_ms);
                    pending_actions.push(
                        PendingAction{action, fill_ts, action_seq, action_seq, issued_demo, action.notional, crossing});
                    ++action_seq;
                    if (issued_demo) {
                        ++demo_sent;
                    }
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
            auto fill = matching_engine.simulate(pending.action, replay.current_book());
            pnl.actions_attempted += 1;
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
            } else {
                handle_reject(fill, pending.demo, pending.target_notional, src);
            }
        }
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

    if (!write_fills_csv(fills_path, fill_rows)) {
        utils::error("Failed to write fills CSV to " + fills_path.string());
        return 1;
    }
    if (!write_metrics_json(metrics_path, run_id, pnl, realized, unrealized, net_total, s1, s10, max_dd, fill_rate,
                            pnl.reject_counts, identity_ok)) {
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
