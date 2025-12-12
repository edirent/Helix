#pragma once

#include "engine/types.hpp"

namespace helix::engine {

class RiskEngine {
  public:
    RiskEngine(double max_position, double max_notional)
        : max_position_(max_position), max_notional_(max_notional) {}

    bool validate(const Action &action, double last_price) const;
    void update(const Fill &fill);
    const Position &position() const { return position_; }

  private:
    Position position_;
    double max_position_{0.0};
    double max_notional_{0.0};
};

}  // namespace helix::engine
