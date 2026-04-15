#include "ui/screen_plan_h.h"
#include "config.h"
#include "theme.h"
#include <cstdio>
#include <ctime>
#include <cmath>

using namespace Theme;

// ── Layout constants (horizontal 320 × 240) ──────────────────────
//
// y=12  CLAUDE PLAN                                   14:32 ●
// y=32  SUBSCRIPTION
//
// ─── col1 (16..168) ───         ─── col2 (176..304) ───
// y=54  SESSION 5H               y=54  WEEKLY LIMIT
// y=70  42% (hero 48pt)          y=70  19% (body 14pt)
//                                y=90  ████░░░░░░
//                                y=98  OPUS 5% SONNET 18%
//                                y=110 RESETS APR 21
// y=124 █████░░░░░░░
// y=134 RESETS 14:32
//
// y=150 ─── divider ───
//
// y=162 EXTRA USAGE    $12.50 / $50 USD
// y=180 ████████░░░░░░░░░░░░░░░░░░
//
// y=206 UPDATED 2M AGO
//
static constexpr int HX_PAD      = 16;
static constexpr int COL1_X      = HX_PAD;
static constexpr int COL1_W      = 150;
static constexpr int COL2_X      = 176;
static constexpr int COL2_W      = 128;
static constexpr int H_CONTENT_W = SCREEN_W_H - 2 * HX_PAD;   // 288

static constexpr int HY_HEADER     = 12;
static constexpr int HY_SUB        = 32;

static constexpr int HY_COL_LBL    = 54;
static constexpr int HY_COL_VAL    = 70;     // col1 hero + col2 body share this top Y
static constexpr int HY_WK_BAR     = 92;
static constexpr int HY_WK_SUB     = 100;
static constexpr int HY_WK_RESET   = 112;

static constexpr int HY_SESS_BAR   = 124;
static constexpr int HY_SESS_RESET = 134;

static constexpr int HY_DIVIDER    = 150;

static constexpr int HY_EX_LINE    = 162;
static constexpr int HY_EX_BAR     = 180;

static constexpr int HY_FOOTER     = 210;

// ── Formatting helpers (mirror ScreenPlan) ───────────────────────

static int clampPct(float pct) {
    if (pct < 0) return 0;
    if (pct > 999) return 999;
    return (int)roundf(pct);
}

static void fmtAgo(char* buf, size_t len, time_t ts) {
    if (ts == 0) { snprintf(buf, len, "NOT YET LOADED"); return; }
    time_t diff = time(nullptr) - ts;
    if (diff < 60)        snprintf(buf, len, "UPDATED JUST NOW");
    else if (diff < 3600) snprintf(buf, len, "UPDATED %ldM AGO", (long)(diff / 60));
    else                  snprintf(buf, len, "UPDATED %ldH AGO", (long)(diff / 3600));
}

static const char* errorHint(uint8_t err) {
    switch (err) {
        case CLAUDE_PLAN_AUTH_FAILED:   return "AUTH EXPIRED";
        case CLAUDE_PLAN_CF_BLOCKED:    return "CF BLOCKED";
        case CLAUDE_PLAN_RATE_LIMITED:  return "RATE LIMITED";
        case CLAUDE_PLAN_NETWORK_ERROR: return "OFFLINE";
        case CLAUDE_PLAN_PARSE_ERROR:   return "API CHANGED";
        default:                        return "";
    }
}

// ── Construction ─────────────────────────────────────────────────

ScreenPlanH::ScreenPlanH() {
    buildLayout();
}

ScreenPlanH::~ScreenPlanH() {}

void ScreenPlanH::buildLayout() {
    scr_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr_, LV_SCROLLBAR_MODE_OFF);

    // Provider name
    lbl_name_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_name_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_name_, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl_name_, 2, 0);
    lv_label_set_text(lbl_name_, "CLAUDE PLAN");
    lv_obj_set_pos(lbl_name_, HX_PAD, HY_HEADER);

    // Status dot
    dot_status_ = lv_obj_create(scr_);
    lv_obj_set_size(dot_status_, Sp::dot_r * 2, Sp::dot_r * 2);
    lv_obj_set_style_radius(dot_status_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_status_, Color::ok(), 0);
    lv_obj_set_style_bg_opa(dot_status_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_status_, 0, 0);
    lv_obj_set_pos(dot_status_, SCREEN_W_H - 60, HY_HEADER + 3);
    lv_obj_set_scrollbar_mode(dot_status_, LV_SCROLLBAR_MODE_OFF);

    // Time
    lbl_time_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_time_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_time_, Color::dim(), 0);
    lv_label_set_text(lbl_time_, "00:00");
    lv_obj_set_pos(lbl_time_, SCREEN_W_H - 48, HY_HEADER);

    // Subtitle
    lbl_sub_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_sub_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_sub_, Color::dim(), 0);
    lv_label_set_text(lbl_sub_, "SUBSCRIPTION");
    lv_obj_set_pos(lbl_sub_, HX_PAD, HY_SUB);

    // ── Left column: session ─────────────────────────────────────
    lbl_session_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_session_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_session_, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl_session_, 2, 0);
    lv_label_set_text(lbl_session_, "SESSION 5H");
    lv_obj_set_pos(lbl_session_, COL1_X, HY_COL_LBL);

    lbl_sess_pct_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_sess_pct_, Font::hero(), 0);
    lv_obj_set_style_text_color(lbl_sess_pct_, Color::text(), 0);
    lv_label_set_text(lbl_sess_pct_, "--%");
    lv_obj_set_pos(lbl_sess_pct_, COL1_X, HY_COL_VAL);

    bar_session_ = lv_bar_create(scr_);
    lv_obj_set_size(bar_session_, COL1_W - 8, Sp::bar_h);
    lv_obj_set_pos(bar_session_, COL1_X, HY_SESS_BAR);
    lv_bar_set_range(bar_session_, 0, 100);
    lv_bar_set_value(bar_session_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_session_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_session_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_session_, Color::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_session_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_session_, Color::claude(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar_session_, Color::darken(Color::claude(), 20), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar_session_, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_session_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar_session_, Anim::countup, LV_PART_MAIN);

    lbl_sess_reset_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_sess_reset_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_sess_reset_, Color::vdim(), 0);
    lv_label_set_text(lbl_sess_reset_, "");
    lv_obj_set_pos(lbl_sess_reset_, COL1_X, HY_SESS_RESET);

    // ── Right column: weekly ─────────────────────────────────────
    lbl_weekly_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_weekly_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_weekly_, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl_weekly_, 2, 0);
    lv_label_set_text(lbl_weekly_, "WEEKLY LIMIT");
    lv_obj_set_pos(lbl_weekly_, COL2_X, HY_COL_LBL);

    lbl_week_pct_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_week_pct_, Font::body(), 0);
    lv_obj_set_style_text_color(lbl_week_pct_, Color::text(), 0);
    lv_label_set_text(lbl_week_pct_, "--%");
    lv_obj_set_pos(lbl_week_pct_, COL2_X, HY_COL_VAL);

    bar_weekly_ = lv_bar_create(scr_);
    lv_obj_set_size(bar_weekly_, COL2_W - 4, Sp::bar_h);
    lv_obj_set_pos(bar_weekly_, COL2_X, HY_WK_BAR);
    lv_bar_set_range(bar_weekly_, 0, 100);
    lv_bar_set_value(bar_weekly_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_weekly_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_weekly_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_weekly_, Color::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_weekly_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_weekly_, Color::claude(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar_weekly_, Color::darken(Color::claude(), 20), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar_weekly_, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_weekly_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar_weekly_, Anim::countup, LV_PART_MAIN);

    lbl_week_sub_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_week_sub_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_week_sub_, Color::vdim(), 0);
    lv_label_set_text(lbl_week_sub_, "");
    lv_obj_set_pos(lbl_week_sub_, COL2_X, HY_WK_SUB);

    lbl_week_reset_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_week_reset_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_week_reset_, Color::vdim(), 0);
    lv_label_set_text(lbl_week_reset_, "");
    lv_obj_set_pos(lbl_week_reset_, COL2_X, HY_WK_RESET);

    // ── Divider ──────────────────────────────────────────────────
    static lv_point_precise_t h_div_pts[2] = {
        {(lv_value_precise_t)HX_PAD, 0},
        {(lv_value_precise_t)(SCREEN_W_H - HX_PAD), 0}
    };
    line_div_ = lv_line_create(scr_);
    lv_line_set_points(line_div_, h_div_pts, 2);
    lv_obj_set_style_line_color(line_div_, Color::hairline(), 0);
    lv_obj_set_style_line_width(line_div_, Sp::hairline, 0);
    lv_obj_set_pos(line_div_, 0, HY_DIVIDER);

    // ── Extra usage strip ────────────────────────────────────────
    lbl_extra_lbl_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_extra_lbl_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_extra_lbl_, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl_extra_lbl_, 2, 0);
    lv_label_set_text(lbl_extra_lbl_, "EXTRA USAGE");
    lv_obj_set_pos(lbl_extra_lbl_, HX_PAD, HY_EX_LINE);

    lbl_extra_val_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_extra_val_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_extra_val_, Color::text(), 0);
    lv_label_set_text(lbl_extra_val_, "—");
    lv_obj_set_pos(lbl_extra_val_, HX_PAD + 110, HY_EX_LINE + 3);

    bar_extra_ = lv_bar_create(scr_);
    lv_obj_set_size(bar_extra_, H_CONTENT_W, Sp::bar_h);
    lv_obj_set_pos(bar_extra_, HX_PAD, HY_EX_BAR);
    lv_bar_set_range(bar_extra_, 0, 100);
    lv_bar_set_value(bar_extra_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_extra_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_extra_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_extra_, Color::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_extra_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_extra_, Color::claude(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_extra_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar_extra_, Anim::countup, LV_PART_MAIN);

    // ── Footer ───────────────────────────────────────────────────
    lbl_footer_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_footer_, Font::small(), 0);
    lv_obj_set_style_text_color(lbl_footer_, Color::vdim(), 0);
    lv_label_set_text(lbl_footer_, "NOT YET LOADED");
    lv_obj_set_pos(lbl_footer_, HX_PAD, HY_FOOTER);
}

// ── Data update ──────────────────────────────────────────────────

void ScreenPlanH::update(const ClaudePlanSnapshot& snap) {
    // Session hero (left column)
    char sessBuf[8];
    snprintf(sessBuf, sizeof(sessBuf), "%d%%", clampPct(snap.session_pct));
    lv_label_set_text(lbl_sess_pct_, sessBuf);
    lv_obj_set_pos(lbl_sess_pct_, COL1_X, HY_COL_VAL);
    lv_bar_set_value(bar_session_,
        (clampPct(snap.session_pct) > 100) ? 100 : clampPct(snap.session_pct),
        LV_ANIM_ON);

    if (snap.session_resets_at > 0) {
        struct tm lt;
        localtime_r(&snap.session_resets_at, &lt);
        char r[24];
        strftime(r, sizeof(r), "RESETS %H:%M", &lt);
        lv_label_set_text(lbl_sess_reset_, r);
    } else {
        lv_label_set_text(lbl_sess_reset_, "");
    }

    // Weekly (right column)
    char weekBuf[8];
    snprintf(weekBuf, sizeof(weekBuf), "%d%%", clampPct(snap.weekly_pct));
    lv_label_set_text(lbl_week_pct_, weekBuf);
    lv_bar_set_value(bar_weekly_,
        (clampPct(snap.weekly_pct) > 100) ? 100 : clampPct(snap.weekly_pct),
        LV_ANIM_ON);

    // Opus / Sonnet breakdown
    char subBuf[48];
    if (snap.weekly_opus_pct > 0.01f || snap.weekly_sonnet_pct > 0.01f) {
        snprintf(subBuf, sizeof(subBuf), "OPUS %d%%  SONNET %d%%",
                 clampPct(snap.weekly_opus_pct),
                 clampPct(snap.weekly_sonnet_pct));
    } else {
        subBuf[0] = '\0';
    }
    lv_label_set_text(lbl_week_sub_, subBuf);

    if (snap.weekly_resets_at > 0) {
        struct tm lt;
        localtime_r(&snap.weekly_resets_at, &lt);
        char r[24];
        strftime(r, sizeof(r), "RESETS %b %d", &lt);
        lv_label_set_text(lbl_week_reset_, r);
    } else {
        lv_label_set_text(lbl_week_reset_, "");
    }

    // Extra usage
    if (snap.extra_enabled) {
        char ex[32];
        snprintf(ex, sizeof(ex), "$%.2f / $%.0f",
                 snap.extra_used_dollars, snap.extra_limit_dollars);
        lv_label_set_text(lbl_extra_val_, ex);
        int exPct = (snap.extra_limit_dollars > 0)
            ? (int)((snap.extra_used_dollars / snap.extra_limit_dollars) * 100)
            : 0;
        if (exPct > 100) exPct = 100;
        lv_bar_set_value(bar_extra_, exPct, LV_ANIM_ON);
    } else {
        lv_label_set_text(lbl_extra_val_, "NOT ENABLED");
        lv_bar_set_value(bar_extra_, 0, LV_ANIM_OFF);
    }

    // Footer
    char foot[48];
    if (snap.error != CLAUDE_PLAN_OK && snap.valid) {
        snprintf(foot, sizeof(foot), "%s  *  CACHED", errorHint(snap.error));
    } else if (snap.error != CLAUDE_PLAN_OK) {
        snprintf(foot, sizeof(foot), "%s", errorHint(snap.error));
    } else {
        fmtAgo(foot, sizeof(foot), snap.last_updated);
    }
    lv_label_set_text(lbl_footer_, foot);

    // Status dot
    if (snap.error != CLAUDE_PLAN_OK) {
        setStatusDot(Color::err());
    } else if (!snap.valid) {
        setStatusDot(Color::warn());
    } else {
        time_t age = time(nullptr) - snap.last_updated;
        if (age > 1800)      setStatusDot(Color::err());
        else if (age > 900)  setStatusDot(Color::warn());
        else                 setStatusDot(Color::ok());
    }
}

void ScreenPlanH::refreshClock() {
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lbl_time_, buf);
}

void ScreenPlanH::setStatusDot(lv_color_t c) {
    lv_obj_set_style_bg_color(dot_status_, c, 0);
}
