#pragma once

#include "engine/types.hpp"

namespace helix::engine {

class DecisionEngine {
  public:
    Action decide(const Feature &feature) const;

    void set_threshold(double t) { threshold_ = t; }
    double threshold() const { return threshold_; }

  private:
    double threshold_{0.01};
};

}  // namespace helix::engine
