#pragma once
#include <lvgl.h>
#include "api/claude_plan.h"

// Common interface for the Claude.ai subscription plan screen. Shared
// between vertical (ScreenPlan) and horizontal (ScreenPlanH) layouts
// so UIManager can dispatch without caring about orientation.
class IScreenPlan {
public:
    virtual ~IScreenPlan() = default;
    virtual void update(const ClaudePlanSnapshot& snap) = 0;
    virtual void refreshClock() = 0;
    virtual lv_obj_t* screen() const = 0;
};
