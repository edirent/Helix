// feature_registry.h

#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "feature_base.h"
#include <mutex>
#include <vector>
#include <utility>
#include <stdexcept>
namespace feat {

using ParamList = std::vector<std::pair<std::string, std::string>>;
using FeatureFactory = std::function<std::unique_ptr<IFeature>(const ParamList&)>;

class FeatureRegistry {
public:
    static FeatureRegistry& instance() {
        static FeatureRegistry inst;
        return inst;
}
    void reg(const std::string& name, FeatureFactory factory) {
        std::lock_guard<std::mutex> g(mu_);
        if (factories_.count(name)) {
            throw std::runtime_error("Feature already registered: " + name);
        }
        factories_[name] = std::move(factory);
    }

    std::unique_ptr<IFeature> create(const std::string& name, const ParamList& params) {
      FeatureFactory f;
      {
        std::lock_guard<std::mutex> g(mu_);
        auto it = factories_.find(name);
        if (it == factories_.end()) throw std::runtime_error("Feature not found: " + name);
        f = it->second; // copy callable
      }
      return f(params); // out of lock
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, FeatureFactory> factories_;
};
}  // namespace feat
