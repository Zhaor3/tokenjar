#pragma once
#include <lvgl.h>
#include "api/codex_plan.h"

// Common interface for the Codex / ChatGPT subscription usage screen.
class IScreenCodexPlan {
public:
    virtual ~IScreenCodexPlan() = default;
    virtual void update(const CodexPlanSnapshot& snap) = 0;
    virtual void refreshClock() = 0;
    virtual lv_obj_t* screen() const = 0;
};
