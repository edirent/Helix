#pragma once

#include "engine/types.hpp"

namespace helix::engine {

class FeatureEngine {
  public:
    Feature compute(const OrderbookSnapshot &book, const TradeTape &tape) const;
};

}  // namespace helix::engine
