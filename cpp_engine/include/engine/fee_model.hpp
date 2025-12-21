#pragma once

#include <string>

#include "engine/types.hpp"

namespace helix::engine {

struct FeeConfig {
    double maker_bps{0.0};   // in basis points
    double taker_bps{0.0};   // in basis points
    std::string fee_ccy{"USDT"};
    std::string rounding{"none"};  // "none" or "ceil_to_cent"
    std::string source{"default"};
};

struct FeeResult {
    double fee{0.0};
    double fee_bps{0.0};
    std::string fee_ccy;
};

class FeeModel {
  public:
    explicit FeeModel(FeeConfig cfg) : cfg_(std::move(cfg)) {}

    FeeResult compute(const Fill &fill) const;
    const FeeConfig &config() const { return cfg_; }

  private:
    double round_fee(double fee) const;

    FeeConfig cfg_;
};

}  // namespace helix::engine
