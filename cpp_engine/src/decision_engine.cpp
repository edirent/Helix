#include "engine/decision_engine.hpp"

namespace helix::engine {

Action DecisionEngine::decide(const Feature &feature) const {
    Action action;
    if (feature.trend_strength > threshold_ && feature.imbalance > 0) {
        action.side = Side::Buy;
        action.size = 1.0;
    } else if (feature.trend_strength < -threshold_ && feature.imbalance < 0) {
        action.side = Side::Sell;
        action.size = 1.0;
    } else {
        action.side = Side::Hold;
        action.size = 0.0;
    }
    return action;
}

}  // namespace helix::engine
