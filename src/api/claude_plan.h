#pragma once
#include <cstdint>
#include <ctime>

// ── Claude.ai subscription plan snapshot ─────────────────────────
//
// This is the *consumer* Pro/Max subscription data scraped from the
// undocumented claude.ai endpoints (sessionKey cookie auth), NOT the
// Admin API platform-usage numbers that UsageSnapshot holds.
//
// Percentages are 0–100 (not 0–1). Reset timestamps are UTC epoch.
// Extra-usage dollars come from the optional overage endpoint and are
// only filled in if `extra_enabled` is true.
//
// On a fetch failure, `valid` stays whatever the cached value was and
// `error` is updated so the screen can show a hint (auth expired,
// Cloudflare challenge, rate-limited, offline, parse error).

enum ClaudePlanError : uint8_t {
    CLAUDE_PLAN_OK            = 0,
    CLAUDE_PLAN_AUTH_FAILED   = 1,   // 401 / 403 with JSON body
    CLAUDE_PLAN_CF_BLOCKED    = 2,   // 403 with HTML body (Cloudflare challenge)
    CLAUDE_PLAN_RATE_LIMITED  = 3,   // 429
    CLAUDE_PLAN_NETWORK_ERROR = 4,   // DNS/TLS/timeout
    CLAUDE_PLAN_PARSE_ERROR   = 5,   // 200 but JSON didn't match shape
};

struct ClaudePlanSnapshot {
    // Utilization percentages (0..100)
    float   session_pct;          // five_hour.utilization
    float   weekly_pct;           // seven_day.utilization
    float   weekly_opus_pct;      // seven_day_opus.utilization (0 if absent)
    float   weekly_sonnet_pct;    // seven_day_sonnet.utilization (0 if absent)

    // Reset timestamps (UTC epoch seconds, 0 if unknown)
    time_t  session_resets_at;
    time_t  weekly_resets_at;

    // Extra-usage / overage billing (only valid when extra_enabled = true)
    float   extra_used_dollars;
    float   extra_limit_dollars;
    bool    extra_enabled;

    // Bookkeeping
    time_t  last_updated;
    bool    valid;                // true once we've successfully fetched /usage at least once
    uint8_t error;                // ClaudePlanError code from the last fetch attempt
};
