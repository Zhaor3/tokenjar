#pragma once
#include <lvgl.h>
#include "api/codex_plan.h"
#include "ui/i_screen_codex_plan.h"

// Codex / ChatGPT subscription usage screen.
//
// Shows the same main windows Codex CLI uses:
//   - primary 5-hour session usage
//   - secondary 7-day weekly usage
//   - highest additional model limit, or code-review limit when present
class ScreenCodexPlan : public IScreenCodexPlan {
    lv_obj_t* scr_             = nullptr;
    lv_obj_t* lbl_name_        = nullptr;
    lv_obj_t* lbl_time_        = nullptr;
    lv_obj_t* dot_status_      = nullptr;
    lv_obj_t* lbl_sub_         = nullptr;

    lv_obj_t* lbl_session_     = nullptr;
    lv_obj_t* lbl_sess_pct_    = nullptr;
    lv_obj_t* bar_session_     = nullptr;
    lv_obj_t* lbl_sess_reset_  = nullptr;

    lv_obj_t* lbl_weekly_      = nullptr;
    lv_obj_t* lbl_week_pct_    = nullptr;
    lv_obj_t* bar_weekly_      = nullptr;
    lv_obj_t* lbl_week_sub_    = nullptr;

    lv_obj_t* line_div_        = nullptr;

    lv_obj_t* lbl_model_lbl_   = nullptr;
    lv_obj_t* lbl_model_val_   = nullptr;
    lv_obj_t* bar_model_       = nullptr;
    lv_obj_t* lbl_model_sub_   = nullptr;

    lv_obj_t* lbl_footer_      = nullptr;

    bool horizontal_;

    int screen_w_ = 240;
    int x_pad_ = 16;
    int content_w_ = 208;
    int col1_x_ = 16;
    int col1_w_ = 208;
    int col2_x_ = 16;
    int col2_w_ = 208;

    int y_header_ = 16;
    int y_sub_ = 32;
    int y_sess_lbl_ = 58;
    int y_sess_pct_ = 74;
    int y_sess_bar_ = 134;
    int y_sess_reset_ = 152;
    int y_week_lbl_ = 190;
    int y_week_pct_ = 206;
    int y_week_bar_ = 232;
    int y_week_sub_ = 250;
    int y_div_ = 176;
    int y_model_line_ = 284;
    int y_model_bar_ = 302;
    int y_model_sub_ = 292;
    int y_footer_ = 310;

    void buildLayout();
    void setLayoutMetrics();
    void setStatusDot(lv_color_t c);

public:
    explicit ScreenCodexPlan(bool horizontal);
    ~ScreenCodexPlan() override = default;

    void update(const CodexPlanSnapshot& snap) override;
    void refreshClock() override;
    lv_obj_t* screen() const override { return scr_; }
};
