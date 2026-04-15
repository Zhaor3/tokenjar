#pragma once
#include <lvgl.h>
#include "api/claude_plan.h"
#include "ui/i_screen_plan.h"

// Horizontal (landscape 320×240) Claude.ai subscription plan screen.
//
// Two-column layout:
//   • Left column (hero):  SESSION 5h %    + bar + reset time
//   • Right column:        WEEKLY LIMIT %  + bar + Opus/Sonnet split
//   • Full-width strip:    EXTRA USAGE dollars + bar
//   • Footer:              "updated Xm ago" or error hint
class ScreenPlanH : public IScreenPlan {
    lv_obj_t* scr_           = nullptr;
    lv_obj_t* lbl_name_      = nullptr;
    lv_obj_t* lbl_time_      = nullptr;
    lv_obj_t* dot_status_    = nullptr;
    lv_obj_t* lbl_sub_       = nullptr;

    // Left column — session
    lv_obj_t* lbl_session_   = nullptr;
    lv_obj_t* lbl_sess_pct_  = nullptr;   // hero
    lv_obj_t* bar_session_   = nullptr;
    lv_obj_t* lbl_sess_reset_= nullptr;

    // Right column — weekly
    lv_obj_t* lbl_weekly_    = nullptr;
    lv_obj_t* lbl_week_pct_  = nullptr;
    lv_obj_t* bar_weekly_    = nullptr;
    lv_obj_t* lbl_week_sub_  = nullptr;   // opus/sonnet
    lv_obj_t* lbl_week_reset_= nullptr;

    lv_obj_t* line_div_      = nullptr;

    // Extra usage strip
    lv_obj_t* lbl_extra_lbl_ = nullptr;
    lv_obj_t* lbl_extra_val_ = nullptr;
    lv_obj_t* bar_extra_     = nullptr;

    lv_obj_t* lbl_footer_    = nullptr;

    void buildLayout();
    void setStatusDot(lv_color_t c);

public:
    ScreenPlanH();
    ~ScreenPlanH() override;

    void update(const ClaudePlanSnapshot& snap) override;
    void refreshClock() override;
    lv_obj_t* screen() const override { return scr_; }
};
