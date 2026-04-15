#pragma once
#include <lvgl.h>
#include "api/usage_provider.h"
#include "ui/i_screen_provider.h"

// Horizontal (landscape, 320×240) provider screen.
// Mirrors ScreenProvider but places the hero dollar amount on the left
// and the sparkline on the right, with the budget/progress strip across
// the lower half of the screen.
class ScreenProviderH : public IScreenProvider {
    lv_obj_t* scr_         = nullptr;
    lv_obj_t* lbl_name_    = nullptr;
    lv_obj_t* lbl_time_    = nullptr;
    lv_obj_t* dot_status_  = nullptr;
    lv_obj_t* lbl_frame_   = nullptr;
    lv_obj_t* lbl_hero_    = nullptr;
    lv_obj_t* line_div_    = nullptr;
    lv_obj_t* lbl_budget_  = nullptr;
    lv_obj_t* bar_budget_  = nullptr;
    lv_obj_t* lbl_tokens_  = nullptr;
    lv_obj_t* canvas_spark_= nullptr;
    lv_obj_t* lbl_footer_  = nullptr;

    lv_color_t  accent_;
    const char* provider_name_;
    uint8_t*    spark_buf_ = nullptr;

    void buildLayout();

public:
    ScreenProviderH(const char* name, lv_color_t accent);
    ~ScreenProviderH() override;

    void update(const UsageSnapshot& snap,
                float daily_budget, float monthly_budget,
                Timeframe tf) override;
    void refreshClock() override;
    void flashHero() override;
    lv_obj_t* screen() const override { return scr_; }

    // Non-interface helpers
    void setStatusDot(lv_color_t color);
    void drawSparkline(const float* data, int count);
};
