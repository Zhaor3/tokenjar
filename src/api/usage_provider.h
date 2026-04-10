#pragma once
#include <cstdint>
#include <ctime>
#include "config.h"

struct UsageSnapshot {
    float   spend_today;
    float   spend_month;
    uint64_t tokens_today;
    uint64_t tokens_month;
    float   hourly_spend[SPARK_POINTS];   // last 24 h, index 0 = oldest
    time_t  last_updated;
    bool    valid;
};

class IUsageProvider {
public:
    virtual ~IUsageProvider() = default;
    virtual bool fetch(const char* apiKey, UsageSnapshot& out) = 0;
    virtual const char* name() const = 0;
};

// Timeframe indices for encoder control
enum class Timeframe : uint8_t {
    H1, H6, H24, D7, D30, _COUNT
};

inline const char* timeframeLabel(Timeframe t) {
    switch (t) {
        case Timeframe::H1:  return "1 HOUR";
        case Timeframe::H6:  return "6 HOURS";
        case Timeframe::H24: return "TODAY";
        case Timeframe::D7:  return "7 DAYS";
        case Timeframe::D30: return "30 DAYS";
        default:             return "TODAY";
    }
}

inline float timeframeSpend(const UsageSnapshot& s, Timeframe t) {
    switch (t) {
        case Timeframe::H1:  return s.hourly_spend[SPARK_POINTS - 1];
        case Timeframe::H6: {
            float sum = 0;
            for (int i = SPARK_POINTS - 6; i < SPARK_POINTS; i++) sum += s.hourly_spend[i];
            return sum;
        }
        case Timeframe::H24: return s.spend_today;
        case Timeframe::D7:  return s.spend_month * 0.233f; // rough 7/30 estimate
        case Timeframe::D30: return s.spend_month;
        default:             return s.spend_today;
    }
}
