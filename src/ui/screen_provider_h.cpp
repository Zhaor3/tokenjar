#include "ui/screen_provider_h.h"
#include "config.h"
#include "theme.h"
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>

using namespace Theme;

// ── Formatting helpers (same as vertical) ────────────────────────

static void fmtCurrency(char* buf, size_t len, float v) {
    if (v >= 10000)     snprintf(buf, len, "$%.0fK", v / 1000.0f);
    else if (v >= 1000) snprintf(buf, len, "$%.1fK", v / 1000.0f);
    else if (v >= 100)  snprintf(buf, len, "$%.0f", v);
    else if (v >= 10)   snprintf(buf, len, "$%.1f", v);
    else                snprintf(buf, len, "$%.2f", v);
}

static void fmtTokens(char* buf, size_t len, uint64_t t) {
    if (t >= 1000000)   snprintf(buf, len, "%.2fM TOKENS", t / 1e6);
    else if (t >= 1000) snprintf(buf, len, "%.1fK TOKENS", t / 1e3);
    else                snprintf(buf, len, "%llu TOKENS", (unsigned long long)t);
}

static void fmtAgo(char* buf, size_t len, time_t ts) {
    if (ts == 0) { snprintf(buf, len, "NO DATA YET"); return; }
    time_t diff = time(nullptr) - ts;
    if (diff < 60)        snprintf(buf, len, "UPDATED JUST NOW");
    else if (diff < 3600) snprintf(buf, len, "UPDATED %ldM AGO", (long)(diff / 60));
    else                  snprintf(buf, len, "UPDATED %ldH AGO", (long)(diff / 3600));
}

// ── Horizontal layout constants (320 × 240) ──────────────────────
//
//  12  CLAUDE                                14:32 ●
//  32  TODAY
//
//  58  $12.34                    ▁▃▅▂▁▃▅▂            (hero + spark)
//
// 128  ───────── hairline divider ───────────────────
// 140  45% OF $30 DAILY
// 158  ████████░░░░░░░░░░░░░░░░
// 176  234.5K TOKENS
// 206  UPDATED 2M AGO
//
static constexpr int HX_PAD        = 16;
static constexpr int H_CONTENT_W   = SCREEN_W_H - 2 * HX_PAD;   // 288

static constexpr int HY_HEADER     = 12;
static constexpr int HY_FRAME      = 32;
static constexpr int HY_HERO       = 58;      // hero label top
static constexpr int HX_SPARK      = 184;     // sparkline column (right half)
static constexpr int HY_SPARK      = 68;      // sparkline row
static constexpr int HY_DIVIDER    = 128;
static constexpr int HY_BUDGET_LBL = 140;
static constexpr int HY_BAR        = 158;
static constexpr int HY_TOKENS     = 176;
static constexpr int HY_FOOTER     = 206;

// ── Construction ─────────────────────────────────────────────────

ScreenProviderH::ScreenProviderH(const char* name, lv_color_t accent)
    : accent_(accent), provider_name_(name)
{
    spark_buf_ = new uint8_t[SPARK_W_H * SPARK_H_H * 2];
    buildLayout();
}

ScreenProviderH::~ScreenProviderH() {
    delete[] spark_buf_;
}

void ScreenProviderH::buildLayout() {
    scr_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr_, LV_SCROLLBAR_MODE_OFF);

    // Provider name — top-left
    lbl_name_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_name_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_name_, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl_name_, 2, 0);
    lv_label_set_text(lbl_name_, provider_name_);
    lv_obj_set_pos(lbl_name_, HX_PAD, HY_HEADER);

    // Status dot — to the left of the clock (top-right)
    dot_status_ = lv_obj_create(scr_);
    lv_obj_set_size(dot_status_, Sp::dot_r * 2, Sp::dot_r * 2);
    lv_obj_set_style_radius(dot_status_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_status_, Color::ok(), 0);
    lv_obj_set_style_bg_opa(dot_status_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_status_, 0, 0);
    lv_obj_set_pos(dot_status_, SCREEN_W_H - 60, HY_HEADER + 3);
    lv_obj_set_scrollbar_mode(dot_status_, LV_SCROLLBAR_MODE_OFF);

    // Time — top-right
    lbl_time_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_time_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_time_, Color::dim(), 0);
    lv_label_set_text(lbl_time_, "00:00");
    lv_obj_set_pos(lbl_time_, SCREEN_W_H - 48, HY_HEADER);

    // Timeframe label — below provider name
    lbl_frame_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_frame_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_frame_, Color::dim(), 0);
    lv_label_set_text(lbl_frame_, "TODAY");
    lv_obj_set_pos(lbl_frame_, HX_PAD, HY_FRAME);

    // Hero dollar amount — left half
    lbl_hero_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_hero_, Font::hero(), 0);
    lv_obj_set_style_text_color(lbl_hero_, Color::text(), 0);
    lv_label_set_text(lbl_hero_, "$0.00");
    lv_obj_set_pos(lbl_hero_, HX_PAD, HY_HERO);

    // Sparkline canvas — right of hero
    canvas_spark_ = lv_canvas_create(scr_);
    lv_canvas_set_buffer(canvas_spark_, spark_buf_, SPARK_W_H, SPARK_H_H, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas_spark_, Color::bg(), LV_OPA_COVER);
    lv_obj_set_pos(canvas_spark_, HX_SPARK, HY_SPARK);

    // Hairline divider — spans full content width
    static lv_point_precise_t h_div_pts[2] = {
        {(lv_value_precise_t)HX_PAD, 0},
        {(lv_value_precise_t)(SCREEN_W_H - HX_PAD), 0}
    };
    line_div_ = lv_line_create(scr_);
    lv_line_set_points(line_div_, h_div_pts, 2);
    lv_obj_set_style_line_color(line_div_, Color::hairline(), 0);
    lv_obj_set_style_line_width(line_div_, Sp::hairline, 0);
    lv_obj_set_pos(line_div_, 0, HY_DIVIDER);

    // Budget label
    lbl_budget_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_budget_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_budget_, Color::dim(), 0);
    lv_label_set_text(lbl_budget_, "0% OF $0 DAILY");
    lv_obj_set_pos(lbl_budget_, HX_PAD, HY_BUDGET_LBL);

    // Progress bar — sharp rectangle, accent gradient
    bar_budget_ = lv_bar_create(scr_);
    lv_obj_set_size(bar_budget_, H_CONTENT_W, Sp::bar_h);
    lv_obj_set_pos(bar_budget_, HX_PAD, HY_BAR);
    lv_bar_set_range(bar_budget_, 0, 100);
    lv_bar_set_value(bar_budget_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_budget_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_budget_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_budget_, Color::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_budget_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_budget_, accent_, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar_budget_, Color::darken(accent_, 20), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar_budget_, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_budget_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar_budget_, Anim::countup, LV_PART_MAIN);

    // Token count
    lbl_tokens_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_tokens_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_tokens_, Color::dim(), 0);
    lv_label_set_text(lbl_tokens_, "0 TOKENS");
    lv_obj_set_pos(lbl_tokens_, HX_PAD, HY_TOKENS);

    // Footer
    lbl_footer_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_footer_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_footer_, Color::vdim(), 0);
    lv_label_set_text(lbl_footer_, "NO DATA YET");
    lv_obj_set_pos(lbl_footer_, HX_PAD, HY_FOOTER);
}

// ── Data update ──────────────────────────────────────────────────

void ScreenProviderH::update(const UsageSnapshot& snap,
                             float daily_budget, float monthly_budget,
                             Timeframe tf)
{
    lv_label_set_text(lbl_frame_, timeframeLabel(tf));

    bool monthly_view = (tf == Timeframe::D7 || tf == Timeframe::D30);
    float budget = monthly_view ? monthly_budget : daily_budget;
    float spend  = timeframeSpend(snap, tf);
    uint64_t tok = snap.tokens_today;
    if (tf == Timeframe::D30) {
        tok = snap.tokens_month;
    } else if (tf == Timeframe::D7) {
        tok = (uint64_t)((double)snap.tokens_month * 7.0 / 30.0);
    }

    // Hero number
    char hero[24];
    fmtCurrency(hero, sizeof(hero), spend);
    lv_label_set_text(lbl_hero_, hero);
    lv_obj_set_pos(lbl_hero_, HX_PAD, HY_HERO);

    // Budget line
    int pct = (budget > 0) ? (int)((spend / budget) * 100) : 0;
    if (pct > 999) pct = 999;
    char budStr[48], budAmt[16];
    fmtCurrency(budAmt, sizeof(budAmt), budget);
    snprintf(budStr, sizeof(budStr), "%d%% OF %s %s",
             pct, budAmt, monthly_view ? "MONTHLY" : "DAILY");
    lv_label_set_text(lbl_budget_, budStr);

    // Progress bar
    lv_bar_set_value(bar_budget_, (pct > 100) ? 100 : pct, LV_ANIM_ON);

    // Tokens
    char tokStr[32];
    fmtTokens(tokStr, sizeof(tokStr), tok);
    lv_label_set_text(lbl_tokens_, tokStr);

    // Sparkline
    drawSparkline(snap.hourly_spend, SPARK_POINTS);

    // Footer
    char foot[40];
    fmtAgo(foot, sizeof(foot), snap.last_updated);
    lv_label_set_text(lbl_footer_, foot);

    // Status dot color
    time_t age = time(nullptr) - snap.last_updated;
    if (!snap.valid || age > 1800)     setStatusDot(Color::err());
    else if (age > 600)                setStatusDot(Color::warn());
    else                               setStatusDot(Color::ok());
}

void ScreenProviderH::refreshClock() {
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lbl_time_, buf);
}

void ScreenProviderH::setStatusDot(lv_color_t c) {
    lv_obj_set_style_bg_color(dot_status_, c, 0);
}

// ── Sparkline drawing ────────────────────────────────────────────

void ScreenProviderH::drawSparkline(const float* data, int count) {
    lv_canvas_fill_bg(canvas_spark_, Color::bg(), LV_OPA_COVER);
    if (count < 2) return;

    // Find max for normalization
    float mx = 0.001f;
    for (int i = 0; i < count; i++)
        if (data[i] > mx) mx = data[i];

    lv_layer_t layer;
    lv_canvas_init_layer(canvas_spark_, &layer);

    for (int i = 1; i < count; i++) {
        int x0 = (i - 1) * (SPARK_W_H - 1) / (count - 1);
        int x1 = i * (SPARK_W_H - 1) / (count - 1);
        int y0 = SPARK_H_H - 1 - (int)((data[i - 1] / mx) * (SPARK_H_H - 2));
        int y1 = SPARK_H_H - 1 - (int)((data[i] / mx) * (SPARK_H_H - 2));

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = accent_;
        dsc.width = 1;
        dsc.p1.x = x0; dsc.p1.y = y0;
        dsc.p2.x = x1; dsc.p2.y = y1;
        lv_draw_line(&layer, &dsc);
    }

    lv_canvas_finish_layer(canvas_spark_, &layer);
}

// ── Press flash feedback ─────────────────────────────────────────

void ScreenProviderH::flashHero() {
    lv_obj_set_style_text_color(lbl_hero_, lv_color_white(), 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_hero_);
    lv_anim_set_values(&a, 255, 245);
    lv_anim_set_duration(&a, Anim::press);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t) {
        lv_obj_set_style_text_color((lv_obj_t*)obj, Color::text(), 0);
    });
    lv_anim_start(&a);
}
