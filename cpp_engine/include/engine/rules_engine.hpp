#pragma once

#include "engine/types.hpp"

namespace helix::engine {

struct RulesConfig {
    double tick_size{0.0};
    double qty_step{0.0};
    double min_qty{0.0};
    double min_notional{0.0};
    double price_band_bps{0.0};  // optional, 0 to disable
    std::string source{"default"};
};

struct RulesResult {
    bool ok{false};
    Action normalized;
    RejectReason reason{RejectReason::None};
};

class RulesEngine {
  public:
    explicit RulesEngine(RulesConfig cfg) : cfg_(cfg) {}

    RulesResult apply(const Action &action, const OrderbookSnapshot &book) const;
    const RulesConfig &config() const { return cfg_; }

  private:
    RulesConfig cfg_;
};

}  // namespace helix::engine
