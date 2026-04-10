#include "ui/screen_provider.h"
#include "config.h"
#include "theme.h"
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>

using namespace Theme;

// ── Formatting helpers ───────────────────────────────────────────

static void fmtCurrency(char* buf, size_t len, float v) {
    if (v >= 10000)     snprintf(buf, len, "$%.0fK", v / 1000.0f);
    else if (v >= 1000) snprintf(buf, len, "$%.1fK", v / 1000.0f);
    else if (v >= 100)  snprintf(buf, len, "$%.0f", v);
    else if (v >= 10)   snprintf(buf, len, "$%.1f", v);
    else                snprintf(buf, len, "$%.2f", v);
}

static void fmtTokens(char* buf, size_t len, uint64_t t) {
    if (t >= 1000000)      snprintf(buf, len, "%.2fM TOKENS", t / 1e6);
    else if (t >= 1000)    snprintf(buf, len, "%.1fK TOKENS", t / 1e3);
    else                   snprintf(buf, len, "%llu TOKENS", (unsigned long long)t);
}

static void fmtAgo(char* buf, size_t len, time_t ts) {
    if (ts == 0) { snprintf(buf, len, "NO DATA YET"); return; }
    time_t diff = time(nullptr) - ts;
    if (diff < 60)        snprintf(buf, len, "UPDATED JUST NOW");
    else if (diff < 3600) snprintf(buf, len, "UPDATED %ldM AGO", (long)(diff / 60));
    else                  snprintf(buf, len, "UPDATED %ldH AGO", (long)(diff / 3600));
}

// ── Layout constants ─────────────────────────────────────────────
static constexpr int Y_HEADER     = 16;
static constexpr int Y_FRAME      = 32;
static constexpr int Y_HERO_MID   = 115;
static constexpr int Y_DIVIDER    = 195;
static constexpr int Y_BUDGET_LBL = 210;
static constexpr int Y_BAR        = 228;
static constexpr int Y_TOKENS     = 248;
static constexpr int Y_SPARK      = 266;
static constexpr int Y_FOOTER     = 298;
static constexpr int X_PAD        = 16;
static constexpr int CONTENT_W    = SCREEN_W - 2 * X_PAD;

// ── Construction ─────────────────────────────────────────────────

ScreenProvider::ScreenProvider(const char* name, lv_color_t accent)
    : accent_(accent), provider_name_(name)
{
    spark_buf_ = new uint8_t[SPARK_W * SPARK_H * 2];
    buildLayout();
}

ScreenProvider::~ScreenProvider() {
    delete[] spark_buf_;
}

void ScreenProvider::buildLayout() {
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
    lv_obj_set_pos(lbl_name_, X_PAD, Y_HEADER);

    // Status dot — near the time
    dot_status_ = lv_obj_create(scr_);
    lv_obj_set_size(dot_status_, Sp::dot_r * 2, Sp::dot_r * 2);
    lv_obj_set_style_radius(dot_status_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_status_, Color::ok(), 0);
    lv_obj_set_style_bg_opa(dot_status_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_status_, 0, 0);
    lv_obj_set_pos(dot_status_, SCREEN_W - 60, Y_HEADER + 3);
    lv_obj_set_scrollbar_mode(dot_status_, LV_SCROLLBAR_MODE_OFF);

    // Time — top-right
    lbl_time_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_time_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_time_, Color::dim(), 0);
    lv_label_set_text(lbl_time_, "00:00");
    lv_obj_set_pos(lbl_time_, SCREEN_W - 48, Y_HEADER);

    // Timeframe label
    lbl_frame_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_frame_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_frame_, Color::dim(), 0);
    lv_label_set_text(lbl_frame_, "TODAY");
    lv_obj_set_pos(lbl_frame_, X_PAD, Y_FRAME);

    // Hero dollar amount — centered
    lbl_hero_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_hero_, Font::hero(), 0);
    lv_obj_set_style_text_color(lbl_hero_, Color::text(), 0);
    lv_label_set_text(lbl_hero_, "$0.00");
    lv_obj_align(lbl_hero_, LV_ALIGN_TOP_MID, 0, Y_HERO_MID - 24);

    // Hairline divider
    static lv_point_precise_t div_pts[2] = {{X_PAD, 0}, {(lv_value_precise_t)(SCREEN_W - X_PAD), 0}};
    line_div_ = lv_line_create(scr_);
    lv_line_set_points(line_div_, div_pts, 2);
    lv_obj_set_style_line_color(line_div_, Color::hairline(), 0);
    lv_obj_set_style_line_width(line_div_, Sp::hairline, 0);
    lv_obj_set_pos(line_div_, 0, Y_DIVIDER);

    // Budget label
    lbl_budget_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_budget_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_budget_, Color::dim(), 0);
    lv_label_set_text(lbl_budget_, "0% OF $0 DAILY");
    lv_obj_set_pos(lbl_budget_, X_PAD, Y_BUDGET_LBL);

    // Progress bar — sharp rectangle, accent gradient
    bar_budget_ = lv_bar_create(scr_);
    lv_obj_set_size(bar_budget_, CONTENT_W, Sp::bar_h);
    lv_obj_set_pos(bar_budget_, X_PAD, Y_BAR);
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
    lv_obj_set_pos(lbl_tokens_, X_PAD, Y_TOKENS);

    // Sparkline canvas
    canvas_spark_ = lv_canvas_create(scr_);
    lv_canvas_set_buffer(canvas_spark_, spark_buf_, SPARK_W, SPARK_H, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas_spark_, Color::bg(), LV_OPA_COVER);
    lv_obj_set_pos(canvas_spark_, X_PAD, Y_SPARK);

    // Footer
    lbl_footer_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_footer_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_footer_, Color::vdim(), 0);
    lv_label_set_text(lbl_footer_, "NO DATA YET");
    lv_obj_set_pos(lbl_footer_, X_PAD, Y_FOOTER);
}

// ── Data update ──────────────────────────────────────────────────

void ScreenProvider::update(const UsageSnapshot& snap,
                            float daily_budget, float monthly_budget,
                            Timeframe tf)
{
    // Timeframe label
    lv_label_set_text(lbl_frame_, timeframeLabel(tf));

    // Decide which values to display
    bool monthly_view = (tf == Timeframe::D7 || tf == Timeframe::D30);
    float budget = monthly_view ? monthly_budget : daily_budget;
    float spend  = monthly_view ? snap.spend_month : timeframeSpend(snap, tf);
    uint64_t tok = monthly_view ? snap.tokens_month : snap.tokens_today;

    // Hero number (animated count-up)
    char hero[24];
    fmtCurrency(hero, sizeof(hero), spend);
    hero_val_ = spend;
    lv_label_set_text(lbl_hero_, hero);
    lv_obj_align(lbl_hero_, LV_ALIGN_TOP_MID, 0, Y_HERO_MID - 24);

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
    drawSparkline(snap.hourly_spend, SPARK_POINTS, SPARK_POINTS);

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

void ScreenProvider::refreshClock() {
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lbl_time_, buf);
}

void ScreenProvider::setStatusDot(lv_color_t c) {
    lv_obj_set_style_bg_color(dot_status_, c, 0);
}

// ── Sparkline drawing ────────────────────────────────────────────

void ScreenProvider::drawSparkline(const float* data, int count, int segments) {
    lv_canvas_fill_bg(canvas_spark_, Color::bg(), LV_OPA_COVER);
    if (count < 2 || segments < 2) return;

    // Find max for normalization
    float mx = 0.001f;
    for (int i = 0; i < count; i++)
        if (data[i] > mx) mx = data[i];

    lv_layer_t layer;
    lv_canvas_init_layer(canvas_spark_, &layer);

    int draw_count = (segments < count) ? segments : count;
    for (int i = 1; i < draw_count; i++) {
        int x0 = (i - 1) * (SPARK_W - 1) / (count - 1);
        int x1 = i * (SPARK_W - 1) / (count - 1);
        int y0 = SPARK_H - 1 - (int)((data[i - 1] / mx) * (SPARK_H - 2));
        int y1 = SPARK_H - 1 - (int)((data[i] / mx) * (SPARK_H - 2));

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

// ── Sparkline reveal animation ───────────────────────────────────

struct SparkAnimCtx {
    ScreenProvider* self;
    float data[SPARK_POINTS];
    int count;
};
static SparkAnimCtx s_sparkCtx;

static void sparkAnimCb(void*, int32_t val) {
    s_sparkCtx.self->drawSparkline(
        s_sparkCtx.data, s_sparkCtx.count, val);
}

void ScreenProvider::animateSparkline(const float* data, int count) {
    s_sparkCtx.self  = this;
    s_sparkCtx.count = count;
    memcpy(s_sparkCtx.data, data, count * sizeof(float));

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, 2, count);
    lv_anim_set_duration(&a, Anim::sparkline);
    lv_anim_set_exec_cb(&a, sparkAnimCb);
    lv_anim_start(&a);
}

// ── Press flash feedback ─────────────────────────────────────────

static void flashRestoreCb(void*, int32_t val) {
    // val goes from 255 → 245, effectively a subtle brightness pulse
}

void ScreenProvider::flashHero() {
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
