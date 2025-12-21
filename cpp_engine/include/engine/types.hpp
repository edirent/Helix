#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace helix::engine {

enum class Side { Buy, Sell, Hold };

struct PriceLevel {
    double price{0.0};
    double qty{0.0};
};

struct OrderbookSnapshot {
    int64_t ts_ms{0};
    double best_bid{0.0};
    double best_ask{0.0};
    double bid_size{0.0};
    double ask_size{0.0};
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

struct TradeTape {
    double last_price{0.0};
    double last_size{0.0};
};

struct TradePrint {
    int64_t ts_ms{0};
    Side side{Side::Hold};  // aggressor side
    double price{0.0};
    double size{0.0};
    std::string trade_id;
};

struct Feature {
    double imbalance{0.0};
    double microprice{0.0};
    double pressure_bid{0.0};
    double pressure_ask{0.0};
    double sweep_signal{0.0};
    double trend_strength{0.0};
};

enum class OrderType : uint8_t { Market, Limit };

enum class ActionKind : uint8_t { Place, Cancel, Replace };

struct Action {
    ActionKind kind{ActionKind::Place};
    uint64_t order_id{0};
    uint64_t target_order_id{0};  // used for cancel/replace
    OrderType type{OrderType::Market};
    Side side{Side::Hold};
    double size{0.0};
    bool is_maker{false};
    double limit_price{0.0};  // optional, used for maker queue model
    double notional{0.0};     // optional quote notional for fee/unit sanity
    bool post_only{false};
    bool reduce_only{false};
    double replace_price{0.0};
    double replace_qty{0.0};
};

enum class Liquidity : uint8_t { Maker, Taker };

enum class FillStatus : uint8_t { Filled, Rejected };

enum class RejectReason : uint8_t {
    None = 0,
    BadSide,
    ZeroQty,
    NoBid,
    NoAsk,
    NoLiquidity,
    MinQty,
    MinNotional,
    PriceInvalid,
    RiskLimit
};

struct Fill {
    uint64_t order_id{0};
    FillStatus status{FillStatus::Rejected};
    RejectReason reason{RejectReason::None};

    double price{0.0};
    double qty{0.0};
    bool partial{false};
    Side side{Side::Hold};
    Liquidity liquidity{Liquidity::Taker};
    double vwap_price{0.0};
    double filled_qty{0.0};
    double unfilled_qty{0.0};
    std::size_t levels_crossed{0};
    double slippage_ticks{0.0};

    static Fill filled(Side s, double px, double q, bool part=false, Liquidity l=Liquidity::Taker) {
        Fill f;
        f.status = FillStatus::Filled;
        f.reason = RejectReason::None;
        f.side = s;
        f.liquidity = l;
        f.price = px;
        f.vwap_price = px;
        f.qty = q;
        f.filled_qty = q;
        f.unfilled_qty = 0.0;
        f.partial = part;
        f.levels_crossed = part ? 1 : 0;
        f.slippage_ticks = 0.0;
        return f;
    }

    static Fill rejected(Side s, RejectReason r) {
        Fill f;
        f.status = FillStatus::Rejected;
        f.reason = r;
        f.side = s;
        f.price = 0.0;
        f.qty = 0.0;
        f.partial = false;
        return f;
    }
};

struct Position {
    double qty{0.0};
    double avg_price{0.0};
    double pnl{0.0};  // legacy accumulated realized pnl
    double realized_pnl{0.0};
};

struct Event {
    enum class Type { Tick, Feature, Decision, Fill, Unknown };
    Type type{Type::Unknown};
    std::string payload;
};

}  // namespace helix::engine
