#pragma once
#include <cstdint>

// ── Pin assignments ──────────────────────────────────────────────
constexpr int PIN_TFT_SCK  = 7;
constexpr int PIN_TFT_MOSI = 9;
constexpr int PIN_TFT_CS   = 5;
constexpr int PIN_TFT_DC   = 4;
constexpr int PIN_TFT_RST  = 6;
constexpr int PIN_TFT_BL   = 10;

constexpr int PIN_ENC_CLK = 1;   // ADC1_CH0, safe GPIO
constexpr int PIN_ENC_DT  = 2;   // ADC1_CH1, safe GPIO
constexpr int PIN_ENC_SW  = 8;   // active low, internal pull-up
                                 // Avoid GPIO 3 (strapping pin for JTAG select)
                                 // Avoid GPIO 0 (BOOT button) and GPIO 19/20 (USB)

// ── Display ──────────────────────────────────────────────────────
// Vertical (portrait) dimensions — rotation 0
constexpr uint16_t SCREEN_W = 240;
constexpr uint16_t SCREEN_H = 320;

// Horizontal (landscape) dimensions — rotation 1
constexpr uint16_t SCREEN_W_H = 320;
constexpr uint16_t SCREEN_H_H = 240;

// Max screen dimension (used for LVGL draw buffer sizing across orientations)
constexpr int SCREEN_MAX_DIM = (SCREEN_W > SCREEN_H) ? SCREEN_W : SCREEN_H;

// ── Budget defaults (override before flashing) ───────────────────
#define DEFAULT_CLAUDE_DAILY_BUDGET   5.0f
#define DEFAULT_CLAUDE_MONTHLY_BUDGET 100.0f
#define DEFAULT_OPENAI_DAILY_BUDGET   5.0f
#define DEFAULT_OPENAI_MONTHLY_BUDGET 100.0f

// ── Timing ───────────────────────────────────────────────────────
constexpr uint32_t API_REFRESH_MS       = 60 * 1000;   // 60 seconds
constexpr uint32_t IDLE_DIM_MS          = 60 * 1000;
constexpr uint32_t IDLE_DEEP_DIM_MS     = 5 * 60 * 1000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT = 30000;
constexpr uint32_t SPLASH_MS            = 1500;
constexpr uint32_t LONG_PRESS_MS        = 1000;

// ── Backlight ────────────────────────────────────────────────────
constexpr uint8_t BL_FULL     = 255;
constexpr uint8_t BL_DIM      = 76;   // ~30 %
constexpr uint8_t BL_DEEP_DIM = 20;   // ~8 %
constexpr uint8_t BL_PWM_CH   = 0;
constexpr uint32_t BL_PWM_FREQ = 5000;
constexpr uint8_t BL_PWM_RES  = 8;

// ── Network ──────────────────────────────────────────────────────
constexpr const char* MDNS_HOST   = "tokenjar";
constexpr const char* AP_SSID     = "tokenjar-setup";
constexpr const char* NTP_SERVER  = "pool.ntp.org";
constexpr const char* DEFAULT_TZ  = "EST5EDT,M3.2.0,M11.1.0";

// ── Sparkline ────────────────────────────────────────────────────
constexpr int SPARK_W      = 208;
constexpr int SPARK_H      = 24;
constexpr int SPARK_POINTS = 24;

// Sparkline dimensions for horizontal layout (side-of-hero sparkline)
constexpr int SPARK_W_H    = 124;
constexpr int SPARK_H_H    = 44;
