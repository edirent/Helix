#pragma once

#include "engine/types.hpp"

namespace helix::engine {

class MatchingEngine {
  public:
    Fill simulate(const Action &action, const OrderbookSnapshot &book) const;
};

}  // namespace helix::engine
