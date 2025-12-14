// imbalance_l1.h
#pragma once
#include <cmath>
#include <cstdlib>
#include <string>
#include "feature_registry.h"

namespace feat {

inline double parse_double_or(const ParamList& params, const std::string& key, double def) {
  for (auto& kv : params) {
    if (kv.first == key) return std::strtod(kv.second.c_str(), nullptr);
  }
  return def;
}

class ImbalanceL1 final : public IFeature {
public:
  explicit ImbalanceL1(const ParamList& params)
    : eps_(parse_double_or(params, "eps", 1e-12)) {

    // 1) spec 必须在构造时固定下来（不可变身份）
    spec_.name = "imbalance_l1";
    spec_.family = "book_imbalance";
    spec_.version = 1;
    spec_.inputs = "L2_TOP";
    spec_.update_mode = "event";
    spec_.unit = "ratio";
    spec_.dtype = "float64";
    spec_.warmup_events = 1;
    spec_.warmup_ns = 0;
    spec_.description = "L1挂单量不对称：(bid_qty-ask_qty)/(bid_qty+ask_qty)";

    // 2) params 必须写进 spec（否则以后你无法复现）
    spec_.params = { {"eps", to_string(eps_)} };

    reset();
  }

  void reset() override {
    seen_ = 0;
    last_bid_qty_ = 0.0;
    last_ask_qty_ = 0.0;
    last_value_ = NaN();
  }

  void on_event(const BookTopEvent& e) override {
    last_bid_qty_ = e.bid_qty;
    last_ask_qty_ = e.ask_qty;
    ++seen_;
    last_value_ = compute();
  }

  bool ready() const override {
    return seen_ >= spec_.warmup_events;
  }

  double value() const override {
    return ready() ? last_value_ : NaN();
  }

  const FeatureSpec& spec() const override { return spec_; }

private:
  double eps_;
  int64_t seen_ = 0;
  double last_bid_qty_ = 0.0;
  double last_ask_qty_ = 0.0;
  double last_value_ = NaN();
  FeatureSpec spec_;

  double compute() const {
    // 关键：任何不可计算都返回 NaN，不要返回 0（会污染统计）
    const double denom = last_bid_qty_ + last_ask_qty_;
    if (!std::isfinite(denom) || std::fabs(denom) <= eps_) return NaN();

    const double num = last_bid_qty_ - last_ask_qty_;
    const double v = num / denom;
    return std::isfinite(v) ? v : NaN();
  }

  static std::string to_string(double x) {
    // 稳定字符串化：避免不同 printf 默认行为导致 spec_id 漂移
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.12g", x);
    return std::string(buf);
  }
};

} // namespace feat

REGISTER_FEATURE(feat::ImbalanceL1, "imbalance_l1");
