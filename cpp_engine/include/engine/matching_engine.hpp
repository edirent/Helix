#pragma once

#include <string>
#include <stdexcept>

#include "engine/types.hpp"

namespace helix::engine {

class MatchingEngine {
  public:
    MatchingEngine(std::string symbol, double tick_size, bool reject_on_insufficient_depth = false)
        : symbol_(std::move(symbol)), tick_size_(tick_size), reject_on_insufficient_depth_(reject_on_insufficient_depth) {
        // tick_size must be explicitly provided per symbol; disallow silent defaults.
        if (symbol_.empty() || tick_size_ <= 0.0) {
            throw std::invalid_argument("MatchingEngine requires valid symbol and positive tick_size");
        }
    }
    Fill simulate(const Action &action, const OrderbookSnapshot &book) const;

  private:
    std::string symbol_;
    double tick_size_{0.0};
    bool reject_on_insufficient_depth_{false};
};

}  // namespace helix::engine
