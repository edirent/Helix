#pragma once

#include <sstream>
#include <string>

#include "engine/types.hpp"

namespace helix::transport {

struct FeatureMessage {
    helix::engine::Feature feature;
    std::string symbol;
    std::string to_string() const;
};

struct ActionMessage {
    helix::engine::Action action;
    std::string symbol;
    std::string to_string() const;
};

}  // namespace helix::transport

inline std::string helix::transport::FeatureMessage::to_string() const {
    std::stringstream ss;
    ss << symbol << " imbalance=" << feature.imbalance << " microprice=" << feature.microprice;
    return ss.str();
}

inline std::string helix::transport::ActionMessage::to_string() const {
    std::stringstream ss;
    ss << symbol << " side=" << static_cast<int>(action.side) << " size=" << action.size;
    return ss.str();
}
