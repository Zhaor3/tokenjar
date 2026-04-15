#pragma once
#include <lvgl.h>
#include "api/claude_plan.h"
#include "ui/i_screen_plan.h"

// Vertical (portrait 240×320) Claude.ai subscription plan screen.
//
// Shows:
//   • SESSION 5h (hero % + bar + reset time)
//   • WEEKLY 7d (body % + bar + Opus/Sonnet split)
//   • EXTRA USAGE ($ used / $ limit, only if enabled)
//   • Footer with "updated Xm ago" or an error hint.
class ScreenPlan : public IScreenPlan {
    lv_obj_t* scr_           = nullptr;
    lv_obj_t* lbl_name_      = nullptr;
    lv_obj_t* lbl_time_      = nullptr;
    lv_obj_t* dot_status_    = nullptr;
    lv_obj_t* lbl_sub_       = nullptr;   // "SUBSCRIPTION"

    lv_obj_t* lbl_session_   = nullptr;   // "SESSION 5H"
    lv_obj_t* lbl_sess_pct_  = nullptr;   // hero "42%"
    lv_obj_t* bar_session_   = nullptr;
    lv_obj_t* lbl_sess_reset_= nullptr;   // "RESETS 23:00"

    lv_obj_t* line_div1_     = nullptr;
    lv_obj_t* lbl_weekly_    = nullptr;   // "WEEKLY LIMIT"
    lv_obj_t* lbl_week_pct_  = nullptr;   // "19%"
    lv_obj_t* bar_weekly_    = nullptr;
    lv_obj_t* lbl_week_sub_  = nullptr;   // "OPUS 5%  SONNET 18%"

    lv_obj_t* line_div2_     = nullptr;
    lv_obj_t* lbl_extra_lbl_ = nullptr;   // "EXTRA USAGE"
    lv_obj_t* lbl_extra_val_ = nullptr;   // "$12.50 / $50.00"
    lv_obj_t* bar_extra_     = nullptr;

    lv_obj_t* lbl_footer_    = nullptr;

    void buildLayout();
    void setStatusDot(lv_color_t c);

public:
    ScreenPlan();
    ~ScreenPlan() override;

    void update(const ClaudePlanSnapshot& snap) override;
    void refreshClock() override;
    lv_obj_t* screen() const override { return scr_; }
};
