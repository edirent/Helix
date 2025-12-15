// feature_base.h

#pragma once
#include <cstdint>
#include <string>   
#include <limits>
#include "feature_spec.h"

namespace feat {

struct BookTopEvent
{
    int64_t ts_ns = 0;
    double bid_px = 0.0;
    double bid_qty = 0.0;
    double ask_px = 0.0;
    double ask_qty = 0.0;
};

class IFeature{
public:
    virtual ~IFeature() = default;
    virtual void update(const BookTopEvent &event) = 0;
    virtual void reset() = 0;
    virtual bool ready() const = 0;
    virtual double value() const = 0;
    virtual const FeatureSpec& spec() const = 0;
protected:
    static double NaN() {
        return std::numeric_limits<double>::quiet_NaN();
    }
};
}  // namespace feat
