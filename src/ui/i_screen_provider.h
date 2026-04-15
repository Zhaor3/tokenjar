#pragma once
#include <lvgl.h>
#include "api/usage_provider.h"

// Common interface shared by vertical (ScreenProvider) and horizontal
// (ScreenProviderH) usage screens. UIManager talks to screens via this
// interface so dispatch stays orientation-agnostic.
class IScreenProvider {
public:
    virtual ~IScreenProvider() = default;
    virtual void update(const UsageSnapshot& snap,
                        float daily_budget, float monthly_budget,
                        Timeframe tf) = 0;
    virtual void refreshClock() = 0;
    virtual void flashHero() = 0;
    virtual lv_obj_t* screen() const = 0;
};
