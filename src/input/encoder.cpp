#include "input/encoder.h"
#include "config.h"
#include <Arduino.h>
#include <ESP32Encoder.h>

static ESP32Encoder* hw = nullptr;

void Encoder::begin() {
    hw = new ESP32Encoder();
    hw->attachHalfQuad(PIN_ENC_DT, PIN_ENC_CLK);
    hw->setCount(0);
    last_count_ = 0;

    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    last_act_ = millis();
}

void Encoder::update() {
    uint32_t now = millis();

    // ── Rotation ─────────────────────────────────────────────────
    if (!hw) return;
    int64_t cnt = hw->getCount();
    int64_t delta = cnt - last_count_;
    if (delta != 0) {
        // One detent = 2 half-quads on most EC11 encoders
        rotation_ += (int)(delta / 2);
        last_count_ = cnt - (delta % 2);
        last_act_ = now;
    }

    // ── Button (active low) ──────────────────────────────────────
    bool pressed = digitalRead(PIN_ENC_SW) == LOW;

    if (pressed && !btn_down_) {
        btn_down_   = true;
        btn_time_   = now;
        long_fired_ = false;
        last_act_   = now;
    }

    if (btn_down_ && pressed && !long_fired_) {
        if (now - btn_time_ >= LONG_PRESS_MS) {
            long_flag_  = true;
            long_fired_ = true;
            last_act_   = now;
        }
    }

    if (!pressed && btn_down_) {
        btn_down_ = false;
        if (!long_fired_) {
            short_flag_ = true;
            last_act_   = now;
        }
    }
}

int Encoder::rotation() {
    int r = rotation_;
    rotation_ = 0;
    return r;
}

bool Encoder::wasPressed() {
    bool f = short_flag_;
    short_flag_ = false;
    return f;
}

bool Encoder::wasLongPressed() {
    bool f = long_flag_;
    long_flag_ = false;
    return f;
}

bool Encoder::isActive() const {
    return (millis() - last_act_) < 500;
}
