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

#include "engine/decision_engine.hpp"
#include "engine/event_bus.hpp"
#include "engine/feature_engine.hpp"
#include "engine/matching_engine.hpp"
#include "engine/recorder.hpp"
#include "engine/risk_engine.hpp"
#include "engine/tick_replay.hpp"
#include "transport/grpc_server.hpp"
#include "transport/zmq_server.hpp"
#include "utils/logger.hpp"

using namespace helix;

namespace {
struct LatencyConfig {
    double base_ms{8.0};
    double jitter_ms{4.0};
    double tail_ms{12.0};
    double tail_prob{0.02};
};

struct PendingAction {
    engine::Action action;
    int64_t fill_ts{0};
    uint64_t seq{0};
    uint64_t action_idx{0};
};

struct PnLAggregate {
    double gross{0.0};
    double fees{0.0};
    std::vector<double> net_steps;
    std::map<int64_t, double> net_by_1s;
    std::map<int64_t, double> net_by_10s;
    double net() const { return gross - fees; }
    static double sharpe_from_buckets(const std::map<int64_t, double> &buckets) {
        if (buckets.empty()) {
            return 0.0;
        }
        double mean = 0.0;
        for (const auto &kv : buckets) {
            mean += kv.second;
        }
        mean /= static_cast<double>(buckets.size());
        double var = 0.0;
        for (const auto &kv : buckets) {
            const double diff = kv.second - mean;
            var += diff * diff;
        }
        var /= static_cast<double>(buckets.size());
        const double stddev = std::sqrt(var) + 1e-9;
        return mean / stddev * std::sqrt(static_cast<double>(buckets.size()));
    }
};

struct MakerParams {
    double q_init{0.8};
    double alpha{0.6};
    int64_t expire_ms{200};
    double adv_ticks{2.0};
};

struct RestingOrder {
    engine::Action action;
    double price{0.0};
    double queue_ahead{0.0};
    double my_qty{0.0};
    int64_t submit_ts{0};
    int64_t expire_ts{0};
};

class MakerQueueSim {
  public:
    explicit MakerQueueSim(MakerParams params, double tick_size)
        : params_(params), tick_size_(tick_size) {}

    void submit(const engine::Action &action, const engine::OrderbookSnapshot &book, int64_t now_ts) {
        RestingOrder ord;
        ord.action = action;
        ord.price = (action.limit_price > 0.0) ? action.limit_price
                                               : (action.side == engine::Side::Buy ? book.best_bid : book.best_ask);
        ord.my_qty = action.size;
        ord.submit_ts = now_ts;
        ord.expire_ts = now_ts + params_.expire_ms;
        ord.queue_ahead = level_qty(book, ord.price, action.side) * params_.q_init;
        orders_.push_back(ord);
    }

    std::vector<engine::Fill> on_book(const engine::OrderbookSnapshot &book, int64_t now_ts) {
        std::vector<engine::Fill> fills;
        update_level_maps(book);

        std::vector<RestingOrder> remaining;
        remaining.reserve(orders_.size());
        for (auto &ord : orders_) {
            const double prev_qty = last_level_qty(ord.price, ord.action.side);
            const double curr_qty = current_level_qty(ord.price, ord.action.side);
            const double delta_down = std::max(0.0, prev_qty - curr_qty);

            if (delta_down > 0.0 && ord.my_qty > 0.0) {
                const double consume_ahead = std::min(ord.queue_ahead, delta_down * params_.alpha);
                ord.queue_ahead -= consume_ahead;
                const double remaining_delta = delta_down - consume_ahead;
                const double fill_qty = std::min(ord.my_qty, remaining_delta);
                ord.my_qty -= fill_qty;
                if (fill_qty > 0.0) {
                    engine::Fill f = engine::Fill::filled(ord.action.side, ord.price, fill_qty,
                                                          ord.my_qty > 0.0, engine::Liquidity::Maker);
                    // Adverse selection penalty: shift fill price against us by adv_ticks.
                    const double penalty = params_.adv_ticks * tick_size_;
                    if (ord.action.side == engine::Side::Buy) {
                        f.price += penalty;
                        f.vwap_price += penalty;
                    } else {
                        f.price -= penalty;
                        f.vwap_price -= penalty;
                    }
                    f.unfilled_qty = ord.my_qty;
                    f.levels_crossed = 1;
                    f.slippage_ticks = 0.0;
                    fills.push_back(f);
                }
            }

            if (ord.my_qty > 0.0 && now_ts >= ord.expire_ts) {
                // expire/cancel remaining
                continue;
            }
            if (ord.my_qty > 0.0) {
                remaining.push_back(ord);
            }
        }

        orders_.swap(remaining);
        last_bids_ = curr_bids_;
        last_asks_ = curr_asks_;
        return fills;
    }

  private:
    double level_qty(const engine::OrderbookSnapshot &book, double price, engine::Side side) const {
        const auto &levels = (side == engine::Side::Buy) ? book.bids : book.asks;
        for (const auto &lvl : levels) {
            if (std::abs(lvl.price - price) < 1e-9) {
                return lvl.qty;
            }
        }
        if (side == engine::Side::Buy && std::abs(price - book.best_bid) < 1e-9) {
            return book.bid_size;
        }
        if (side == engine::Side::Sell && std::abs(price - book.best_ask) < 1e-9) {
            return book.ask_size;
        }
        return 0.0;
    }

    void update_level_maps(const engine::OrderbookSnapshot &book) {
        curr_bids_.clear();
        curr_asks_.clear();
        for (const auto &lvl : book.bids) {
            curr_bids_[lvl.price] = lvl.qty;
        }
        for (const auto &lvl : book.asks) {
            curr_asks_[lvl.price] = lvl.qty;
        }
    }

    double current_level_qty(double price, engine::Side side) const {
        if (side == engine::Side::Buy) {
            auto it = curr_bids_.find(price);
            return it == curr_bids_.end() ? 0.0 : it->second;
        }
        auto it = curr_asks_.find(price);
        return it == curr_asks_.end() ? 0.0 : it->second;
    }

    double last_level_qty(double price, engine::Side side) const {
        if (side == engine::Side::Buy) {
            auto it = last_bids_.find(price);
            return it == last_bids_.end() ? current_level_qty(price, side) : it->second;
        }
        auto it = last_asks_.find(price);
        return it == last_asks_.end() ? current_level_qty(price, side) : it->second;
    }

    MakerParams params_;
    std::vector<RestingOrder> orders_;
    std::map<double, double, std::greater<double>> last_bids_;
    std::map<double, double, std::less<double>> last_asks_;
    std::map<double, double, std::greater<double>> curr_bids_;
    std::map<double, double, std::less<double>> curr_asks_;
    double tick_size_{0.0};
};

double deterministic_latency_ms(const std::string &symbol, uint64_t seq, uint64_t action_idx, const LatencyConfig &cfg) {
    std::string seed_input = symbol + "#" + std::to_string(seq) + "#" + std::to_string(action_idx);
    std::size_t seed_val = std::hash<std::string>{}(seed_input);
    std::mt19937_64 rng(static_cast<uint64_t>(seed_val));
    std::uniform_real_distribution<double> jitter_dist(0.0, cfg.jitter_ms);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    double lat = cfg.base_ms + jitter_dist(rng);
    if (u01(rng) < cfg.tail_prob) {
        lat += cfg.tail_ms;
    }
    return lat;
}

struct PendingComparator {
    bool operator()(const PendingAction &a, const PendingAction &b) const { return a.fill_ts > b.fill_ts; }
};
}  // namespace

int main(int argc, char **argv) {
    std::string replay_source = "data/replay/synthetic.csv";
    if (argc > 1) {
        replay_source = argv[1];
    }

    engine::EventBus bus(64);
    engine::TickReplay replay;
    replay.load_file(replay_source);

    engine::FeatureEngine feature_engine;
    engine::DecisionEngine decision_engine;
    engine::RiskEngine risk_engine(5.0, 250000.0);
    const double tick_size = 0.01;  // explicit per-symbol tick size; do not rely on defaults
    const std::string symbol = "SIM";
    engine::MatchingEngine matching_engine(symbol, tick_size);
    engine::Recorder recorder("engine_events.log");
    engine::TradeTape tape{100.0, 1.0};
    LatencyConfig latency_cfg;
    uint64_t action_seq = 0;
    std::priority_queue<PendingAction, std::vector<PendingAction>, PendingComparator> pending_actions;
    PnLAggregate pnl;
    MakerParams maker_params{};
    MakerQueueSim maker_sim(maker_params, tick_size);

    transport::ZmqServer feature_pub("tcp://*:7001");
    transport::GrpcServer action_pub("0.0.0.0:50051");
    feature_pub.start();
    action_pub.start();

    while (replay.feed_next(bus)) {
        auto evt = bus.poll();
        if (!evt) {
            continue;
        }
        recorder.record(*evt);

        // Update maker queue fills against the latest book.
        const int64_t now_ts = replay.current_book().ts_ms;
        for (auto &fill : maker_sim.on_book(replay.current_book(), now_ts)) {
            if (fill.status == engine::FillStatus::Filled) {
                double mark = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
                if (mark <= 0.0) {
                    mark = fill.vwap_price;
                }
                const double prev_mark_pnl =
                    risk_engine.position().pnl + risk_engine.position().qty * (mark - risk_engine.position().avg_price);
                risk_engine.update(fill);
                const double new_mark_pnl =
                    risk_engine.position().pnl + risk_engine.position().qty * (mark - risk_engine.position().avg_price);
                const double gross_delta = new_mark_pnl - prev_mark_pnl;
                const double fee_rate = 0.0002;  // maker
                const double notional = fill.vwap_price * fill.filled_qty;
                const double fee_paid = notional * fee_rate;
                pnl.gross += gross_delta;
                pnl.fees += fee_paid;
                const double net_delta = gross_delta - fee_paid;
                pnl.net_steps.push_back(net_delta);
                const double mid = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
                const double spread_paid_ticks =
                    (mid > 0.0) ? std::abs(fill.vwap_price - mid) / tick_size : 0.0;
                const int64_t bucket_1s = now_ts / 1000;
                const int64_t bucket_10s = now_ts / 10000;
                pnl.net_by_1s[bucket_1s] += net_delta;
                pnl.net_by_10s[bucket_10s] += net_delta;
                std::stringstream ss;
                ss << "fill side=" << static_cast<int>(fill.side) << " vwap=" << fill.vwap_price
                   << " filled=" << fill.filled_qty << " unfilled=" << fill.unfilled_qty
                   << " levels=" << fill.levels_crossed << " slip_ticks=" << fill.slippage_ticks
                   << " partial=" << (fill.partial ? 1 : 0) << " spread_paid_ticks=" << spread_paid_ticks
                   << " liq=" << (fill.liquidity == engine::Liquidity::Maker ? "M" : "T")
                   << " adv_ticks=" << (fill.liquidity == engine::Liquidity::Maker ? maker_params.adv_ticks : 0.0)
                   << " fee=" << fee_paid << " gross=" << gross_delta << " net=" << net_delta
                   << " fees_tot=" << pnl.fees << " net_tot=" << pnl.net();
                bus.publish(engine::Event{engine::Event::Type::Fill, ss.str()});
                utils::info(ss.str());
            }
        }

        // Process pending actions whose fill_ts <= current book ts.
        // (use same now_ts)
        while (!pending_actions.empty() && pending_actions.top().fill_ts <= now_ts) {
            const auto pending = pending_actions.top();
            pending_actions.pop();
            auto fill = matching_engine.simulate(pending.action, replay.current_book());
            if (fill.status == engine::FillStatus::Filled) {
                double mark = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
                if (mark <= 0.0) {
                    mark = fill.vwap_price;
                }
                const double prev_mark_pnl =
                    risk_engine.position().pnl + risk_engine.position().qty * (mark - risk_engine.position().avg_price);
                risk_engine.update(fill);
                const double new_mark_pnl =
                    risk_engine.position().pnl + risk_engine.position().qty * (mark - risk_engine.position().avg_price);
                const double gross_delta = new_mark_pnl - prev_mark_pnl;

                const double fee_rate = (fill.liquidity == engine::Liquidity::Maker) ? 0.0002 : 0.0006;
                const double notional = fill.vwap_price * fill.filled_qty;
                const double fee_paid = notional * fee_rate;
                pnl.gross += gross_delta;
                pnl.fees += fee_paid;
                const double net_delta = gross_delta - fee_paid;
                pnl.net_steps.push_back(net_delta);
                const double mid = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
                const double spread_paid_ticks =
                    (mid > 0.0) ? std::abs(fill.vwap_price - mid) / tick_size : 0.0;
                const int64_t bucket_1s = now_ts / 1000;
                const int64_t bucket_10s = now_ts / 10000;
                pnl.net_by_1s[bucket_1s] += net_delta;
                pnl.net_by_10s[bucket_10s] += net_delta;

                std::stringstream ss;
                ss << "fill side=" << static_cast<int>(fill.side) << " vwap=" << fill.vwap_price
                   << " filled=" << fill.filled_qty << " unfilled=" << fill.unfilled_qty
                   << " levels=" << fill.levels_crossed << " slip_ticks=" << fill.slippage_ticks
                   << " partial=" << (fill.partial ? 1 : 0) << " spread_paid_ticks=" << spread_paid_ticks
                   << " liq=" << (fill.liquidity == engine::Liquidity::Maker ? "M" : "T")
                   << " adv_ticks=" << (fill.liquidity == engine::Liquidity::Maker ? maker_params.adv_ticks : 0.0)
                   << " fee=" << fee_paid << " gross=" << gross_delta << " net=" << net_delta
                   << " fees_tot=" << pnl.fees << " net_tot=" << pnl.net();
                bus.publish(engine::Event{engine::Event::Type::Fill, ss.str()});
                utils::info(ss.str());
                action_pub.publish({pending.action, symbol});
            } else {
                std::stringstream ss;
                ss << "fill_reject side=" << static_cast<int>(fill.side) << " reason="
                   << static_cast<int>(fill.reason);
                bus.publish(engine::Event{engine::Event::Type::Fill, ss.str()});
            }
        }

        auto feature = feature_engine.compute(replay.current_book(), tape);
        feature_pub.publish({feature, "SIM"});

        auto action = decision_engine.decide(feature);
        if (risk_engine.validate(action, replay.current_book().best_ask)) {
            if (action.is_maker) {
                maker_sim.submit(action, replay.current_book(), now_ts);
            } else {
                const double latency_ms = deterministic_latency_ms(symbol, action_seq, action_seq, latency_cfg);
                const int64_t fill_ts = now_ts + static_cast<int64_t>(latency_ms);
                pending_actions.push(PendingAction{action, fill_ts, action_seq, action_seq});
                ++action_seq;
            }
        }
    }

    // Flush any remaining pending actions against last known book state.
    while (!pending_actions.empty()) {
        const auto pending = pending_actions.top();
        pending_actions.pop();
        auto fill = matching_engine.simulate(pending.action, replay.current_book());
        if (fill.status == engine::FillStatus::Filled) {
            double mark = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
            if (mark <= 0.0) {
                mark = fill.vwap_price;
            }
            const double prev_mark_pnl =
                risk_engine.position().pnl + risk_engine.position().qty * (mark - risk_engine.position().avg_price);
            risk_engine.update(fill);
            const double new_mark_pnl =
                risk_engine.position().pnl + risk_engine.position().qty * (mark - risk_engine.position().avg_price);
            const double gross_delta = new_mark_pnl - prev_mark_pnl;
            const double fee_rate = (fill.liquidity == engine::Liquidity::Maker) ? 0.0002 : 0.0006;
            const double notional = fill.vwap_price * fill.filled_qty;
            const double fee_paid = notional * fee_rate;
            pnl.gross += gross_delta;
            pnl.fees += fee_paid;
            const double net_delta = gross_delta - fee_paid;
            pnl.net_steps.push_back(net_delta);
            const double mid = (replay.current_book().best_bid + replay.current_book().best_ask) / 2.0;
            const double spread_paid_ticks = (mid > 0.0) ? std::abs(fill.vwap_price - mid) / tick_size : 0.0;
            pnl.net_by_1s[(replay.current_book().ts_ms) / 1000] += net_delta;
            pnl.net_by_10s[(replay.current_book().ts_ms) / 10000] += net_delta;
            std::stringstream ss;
            ss << "fill side=" << static_cast<int>(fill.side) << " vwap=" << fill.vwap_price << " filled="
               << fill.filled_qty << " unfilled=" << fill.unfilled_qty << " levels=" << fill.levels_crossed
               << " slip_ticks=" << fill.slippage_ticks << " partial=" << (fill.partial ? 1 : 0)
               << " spread_paid_ticks=" << spread_paid_ticks << " liq="
               << (fill.liquidity == engine::Liquidity::Maker ? "M" : "T")
               << " adv_ticks=" << (fill.liquidity == engine::Liquidity::Maker ? maker_params.adv_ticks : 0.0)
               << " fee=" << fee_paid << " gross=" << gross_delta << " net=" << net_delta
               << " fees_tot=" << pnl.fees << " net_tot=" << pnl.net();
            bus.publish(engine::Event{engine::Event::Type::Fill, ss.str()});
            utils::info(ss.str());
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
    summary << " net_sharpe_1s=" << PnLAggregate::sharpe_from_buckets(pnl.net_by_1s)
            << " net_sharpe_10s=" << PnLAggregate::sharpe_from_buckets(pnl.net_by_10s);
    utils::info(summary.str());

    recorder.flush();
    feature_pub.stop();
    action_pub.stop();
    utils::info("Engine run complete.");
    return 0;
}
