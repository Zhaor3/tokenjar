#include "ui/screen_codex_plan.h"
#include "config.h"
#include "theme.h"
#include <cstdio>
#include <ctime>
#include <cmath>
#include <ctype.h>
#include <string.h>

using namespace Theme;

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
        case CODEX_PLAN_AUTH_FAILED:   return "AUTH EXPIRED";
        case CODEX_PLAN_RATE_LIMITED:  return "RATE LIMITED";
        case CODEX_PLAN_NETWORK_ERROR: return "OFFLINE";
        case CODEX_PLAN_PARSE_ERROR:   return "API CHANGED";
        case CODEX_PLAN_WRONG_TOKEN:   return "NOT API KEY";
        default:                       return "";
    }
}

static void formatPlan(char* out, size_t len, const char* plan) {
    if (!plan || !*plan) {
        snprintf(out, len, "CHATGPT / CODEX");
        return;
    }
    char upper[16];
    size_t i = 0;
    for (; i < sizeof(upper) - 1 && plan[i]; ++i) {
        upper[i] = (char)toupper((unsigned char)plan[i]);
    }
    upper[i] = '\0';
    snprintf(out, len, "CHATGPT %s", upper);
}

static lv_obj_t* makeLabel(lv_obj_t* parent,
                           const lv_font_t* font,
                           lv_color_t color,
                           const char* text,
                           int x,
                           int y)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

static lv_obj_t* makeBar(lv_obj_t* parent, int x, int y, int w) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, Sp::bar_h);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, Color::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, Color::openai(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar, Color::darken(Color::openai(), 20), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar, Anim::countup, LV_PART_MAIN);
    return bar;
}

ScreenCodexPlan::ScreenCodexPlan(bool horizontal)
    : horizontal_(horizontal)
{
    buildLayout();
}

void ScreenCodexPlan::setLayoutMetrics() {
    if (horizontal_) {
        screen_w_ = SCREEN_W_H;
        x_pad_ = 16;
        content_w_ = SCREEN_W_H - 2 * x_pad_;
        col1_x_ = x_pad_;
        col1_w_ = 150;
        col2_x_ = 176;
        col2_w_ = 128;

        y_header_ = 12;
        y_sub_ = 32;
        y_sess_lbl_ = 54;
        y_sess_pct_ = 70;
        y_sess_bar_ = 124;
        y_sess_reset_ = 134;
        y_week_lbl_ = 54;
        y_week_pct_ = 70;
        y_week_bar_ = 92;
        y_week_sub_ = 104;
        y_div_ = 150;
        y_model_line_ = 162;
        y_model_bar_ = 180;
        y_model_sub_ = 194;
        y_footer_ = 210;
    } else {
        screen_w_ = SCREEN_W;
        x_pad_ = 16;
        content_w_ = SCREEN_W - 2 * x_pad_;
        col1_x_ = x_pad_;
        col1_w_ = content_w_;
        col2_x_ = x_pad_;
        col2_w_ = content_w_;

        y_header_ = 16;
        y_sub_ = 32;
        y_sess_lbl_ = 58;
        y_sess_pct_ = 74;
        y_sess_bar_ = 134;
        y_sess_reset_ = 152;
        y_div_ = 176;
        y_week_lbl_ = 190;
        y_week_pct_ = 206;
        y_week_bar_ = 232;
        y_week_sub_ = 250;
        y_model_line_ = 284;
        y_model_bar_ = 302;
        y_model_sub_ = 292;
        y_footer_ = 310;
    }
}

void ScreenCodexPlan::buildLayout() {
    setLayoutMetrics();

    scr_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr_, LV_SCROLLBAR_MODE_OFF);

    lbl_name_ = makeLabel(scr_, Font::label(), Color::dim(),
                          "OPENAI SESSION", x_pad_, y_header_);
    lv_obj_set_style_text_letter_space(lbl_name_, 2, 0);

    dot_status_ = lv_obj_create(scr_);
    lv_obj_set_size(dot_status_, Sp::dot_r * 2, Sp::dot_r * 2);
    lv_obj_set_style_radius(dot_status_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_status_, Color::ok(), 0);
    lv_obj_set_style_bg_opa(dot_status_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_status_, 0, 0);
    lv_obj_set_pos(dot_status_, screen_w_ - 60, y_header_ + 3);
    lv_obj_set_scrollbar_mode(dot_status_, LV_SCROLLBAR_MODE_OFF);

    lbl_time_ = makeLabel(scr_, Font::label(), Color::dim(),
                          "00:00", screen_w_ - 48, y_header_);

    lbl_sub_ = makeLabel(scr_, Font::label(), Color::dim(),
                         "CHATGPT / CODEX", x_pad_, y_sub_);

    lbl_session_ = makeLabel(scr_, Font::label(), Color::dim(),
                             "SESSION 5H", col1_x_, y_sess_lbl_);
    lv_obj_set_style_text_letter_space(lbl_session_, 2, 0);

    lbl_sess_pct_ = makeLabel(scr_, Font::hero(), Color::text(),
                              "--%", col1_x_, y_sess_pct_);
    if (!horizontal_) lv_obj_align(lbl_sess_pct_, LV_ALIGN_TOP_MID, 0, y_sess_pct_);

    bar_session_ = makeBar(scr_, col1_x_, y_sess_bar_,
                           horizontal_ ? (col1_w_ - 8) : content_w_);

    lbl_sess_reset_ = makeLabel(scr_, Font::small(), Color::vdim(),
                                "", col1_x_, y_sess_reset_);

    if (horizontal_) {
        lbl_weekly_ = makeLabel(scr_, Font::label(), Color::dim(),
                                "WEEKLY LIMIT", col2_x_, y_week_lbl_);
        lv_obj_set_style_text_letter_space(lbl_weekly_, 2, 0);
        lbl_week_pct_ = makeLabel(scr_, Font::body(), Color::text(),
                                  "--%", col2_x_, y_week_pct_);
        bar_weekly_ = makeBar(scr_, col2_x_, y_week_bar_, col2_w_ - 4);
        lbl_week_sub_ = makeLabel(scr_, Font::small(), Color::vdim(),
                                  "", col2_x_, y_week_sub_);
    } else {
        static lv_point_precise_t v_div_pts[2] = {
            {(lv_value_precise_t)16, 0},
            {(lv_value_precise_t)(SCREEN_W - 16), 0}
        };
        line_div_ = lv_line_create(scr_);
        lv_line_set_points(line_div_, v_div_pts, 2);
        lv_obj_set_style_line_color(line_div_, Color::hairline(), 0);
        lv_obj_set_style_line_width(line_div_, Sp::hairline, 0);
        lv_obj_set_pos(line_div_, 0, y_div_);

        lbl_weekly_ = makeLabel(scr_, Font::label(), Color::dim(),
                                "WEEKLY LIMIT", col2_x_, y_week_lbl_);
        lv_obj_set_style_text_letter_space(lbl_weekly_, 2, 0);
        lbl_week_pct_ = makeLabel(scr_, Font::body(), Color::text(),
                                  "--%", col2_x_, y_week_pct_);
        bar_weekly_ = makeBar(scr_, col2_x_, y_week_bar_, content_w_);
        lbl_week_sub_ = makeLabel(scr_, Font::small(), Color::vdim(),
                                  "", col2_x_, y_week_sub_);
    }

    if (horizontal_) {
        static lv_point_precise_t h_div_pts[2] = {
            {(lv_value_precise_t)16, 0},
            {(lv_value_precise_t)(SCREEN_W_H - 16), 0}
        };
        line_div_ = lv_line_create(scr_);
        lv_line_set_points(line_div_, h_div_pts, 2);
        lv_obj_set_style_line_color(line_div_, Color::hairline(), 0);
        lv_obj_set_style_line_width(line_div_, Sp::hairline, 0);
        lv_obj_set_pos(line_div_, 0, y_div_);
    }

    lbl_model_lbl_ = makeLabel(scr_, Font::label(), Color::dim(),
                               "MODEL LIMIT", x_pad_, y_model_line_);
    lv_obj_set_style_text_letter_space(lbl_model_lbl_, 2, 0);

    lbl_model_val_ = makeLabel(scr_, Font::small(), Color::text(),
                               "NONE", x_pad_ + 110, y_model_line_ + 3);
    bar_model_ = makeBar(scr_, x_pad_, y_model_bar_, content_w_);
    lbl_model_sub_ = makeLabel(scr_, Font::small(), Color::vdim(),
                               "", x_pad_, y_model_sub_);

    lbl_footer_ = makeLabel(scr_, Font::small(), Color::vdim(),
                            "NOT YET LOADED", x_pad_, y_footer_);
}

void ScreenCodexPlan::update(const CodexPlanSnapshot& snap) {
    char sub[32];
    formatPlan(sub, sizeof(sub), snap.plan_type);
    lv_label_set_text(lbl_sub_, sub);

    if (!snap.valid) {
        lv_label_set_text(lbl_sess_pct_, "--%");
        if (horizontal_) lv_obj_set_pos(lbl_sess_pct_, col1_x_, y_sess_pct_);
        else             lv_obj_align(lbl_sess_pct_, LV_ALIGN_TOP_MID, 0, y_sess_pct_);
        lv_bar_set_value(bar_session_, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_sess_reset_, "");
        lv_label_set_text(lbl_week_pct_, "--%");
        lv_bar_set_value(bar_weekly_, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_week_sub_, "");
        lv_label_set_text(lbl_model_lbl_, "MODEL LIMIT");
        lv_label_set_text(lbl_model_val_, "NONE");
        lv_bar_set_value(bar_model_, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_model_sub_, "");
        const char* hint = errorHint(snap.error);
        lv_label_set_text(lbl_footer_, hint[0] ? hint : "NOT YET LOADED");
        setStatusDot(snap.error == CODEX_PLAN_OK ? Color::warn() : Color::err());
        return;
    }

    char sessBuf[8];
    snprintf(sessBuf, sizeof(sessBuf), "%d%%", clampPct(snap.session_pct));
    lv_label_set_text(lbl_sess_pct_, sessBuf);
    if (horizontal_) lv_obj_set_pos(lbl_sess_pct_, col1_x_, y_sess_pct_);
    else             lv_obj_align(lbl_sess_pct_, LV_ALIGN_TOP_MID, 0, y_sess_pct_);
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

    char weekBuf[8];
    snprintf(weekBuf, sizeof(weekBuf), "%d%%", clampPct(snap.weekly_pct));
    lv_label_set_text(lbl_week_pct_, weekBuf);
    lv_bar_set_value(bar_weekly_,
        (clampPct(snap.weekly_pct) > 100) ? 100 : clampPct(snap.weekly_pct),
        LV_ANIM_ON);

    if (snap.weekly_resets_at > 0) {
        struct tm lt;
        localtime_r(&snap.weekly_resets_at, &lt);
        char r[32];
        strftime(r, sizeof(r), horizontal_ ? "RESETS %b %d" : "RESETS %b %d", &lt);
        lv_label_set_text(lbl_week_sub_, r);
    } else {
        lv_label_set_text(lbl_week_sub_, "");
    }

    if (snap.has_model_limit) {
        lv_label_set_text(lbl_model_lbl_, "MODEL LIMIT");
        char val[12];
        snprintf(val, sizeof(val), "%d%%", clampPct(snap.model_pct));
        lv_label_set_text(lbl_model_val_, val);
        lv_bar_set_value(bar_model_,
            (clampPct(snap.model_pct) > 100) ? 100 : clampPct(snap.model_pct),
            LV_ANIM_ON);

        char detail[48];
        if (snap.model_resets_at > 0) {
            struct tm lt;
            localtime_r(&snap.model_resets_at, &lt);
            char r[12];
            strftime(r, sizeof(r), "%b %d", &lt);
            snprintf(detail, sizeof(detail), "%s  RESETS %s",
                     snap.model_name[0] ? snap.model_name : "MODEL", r);
        } else {
            snprintf(detail, sizeof(detail), "%s",
                     snap.model_name[0] ? snap.model_name : "MODEL");
        }
        lv_label_set_text(lbl_model_sub_, detail);
    } else if (snap.has_code_review) {
        lv_label_set_text(lbl_model_lbl_, "CODE REVIEW");
        char val[12];
        snprintf(val, sizeof(val), "%d%%", clampPct(snap.code_review_pct));
        lv_label_set_text(lbl_model_val_, val);
        lv_bar_set_value(bar_model_,
            (clampPct(snap.code_review_pct) > 100) ? 100 : clampPct(snap.code_review_pct),
            LV_ANIM_ON);
        lv_label_set_text(lbl_model_sub_, "");
    } else {
        lv_label_set_text(lbl_model_lbl_, "MODEL LIMIT");
        lv_label_set_text(lbl_model_val_, "NONE");
        lv_bar_set_value(bar_model_, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_model_sub_, "");
    }

    char foot[48];
    if (snap.error != CODEX_PLAN_OK && snap.valid) {
        snprintf(foot, sizeof(foot), "%s  *  CACHED", errorHint(snap.error));
    } else if (snap.error != CODEX_PLAN_OK) {
        snprintf(foot, sizeof(foot), "%s", errorHint(snap.error));
    } else {
        fmtAgo(foot, sizeof(foot), snap.last_updated);
    }
    lv_label_set_text(lbl_footer_, foot);

    if (snap.error != CODEX_PLAN_OK) {
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

void ScreenCodexPlan::refreshClock() {
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lbl_time_, buf);
}

void ScreenCodexPlan::setStatusDot(lv_color_t c) {
    lv_obj_set_style_bg_color(dot_status_, c, 0);
}
