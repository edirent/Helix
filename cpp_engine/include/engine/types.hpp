#pragma once

#include <string>

namespace helix::engine {

struct OrderbookSnapshot {
    double best_bid{0.0};
    double best_ask{0.0};
    double bid_size{0.0};
    double ask_size{0.0};
};

struct TradeTape {
    double last_price{0.0};
    double last_size{0.0};
};

struct Feature {
    double imbalance{0.0};
    double microprice{0.0};
    double pressure_bid{0.0};
    double pressure_ask{0.0};
    double sweep_signal{0.0};
    double trend_strength{0.0};
};

enum class Side { Buy, Sell, Hold };

struct Action {
    Side side{Side::Hold};
    double size{0.0};
};

struct Fill {
    double price{0.0};
    double qty{0.0};
    bool partial{false};
    Side side{Side::Hold};
};

struct Position {
    double qty{0.0};
    double avg_price{0.0};
    double pnl{0.0};
};

struct Event {
    enum class Type { Tick, Feature, Decision, Fill, Unknown };
    Type type{Type::Unknown};
    std::string payload;
};

}  // namespace helix::engine
