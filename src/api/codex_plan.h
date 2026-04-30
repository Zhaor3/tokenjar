#pragma once
#include <cstdint>
#include <ctime>

// ── OpenAI Codex / ChatGPT subscription usage snapshot ───────────
//
// This is the ChatGPT-plan Codex rate-limit data used by Codex CLI/App,
// not the OpenAI Platform Admin API billing data. The endpoint is an
// undocumented ChatGPT backend endpoint and requires Codex OAuth tokens.
//
// Percentages are 0-100. Reset timestamps are UTC epoch seconds.

enum CodexPlanError : uint8_t {
    CODEX_PLAN_OK            = 0,
    CODEX_PLAN_AUTH_FAILED   = 1,
    CODEX_PLAN_RATE_LIMITED  = 2,
    CODEX_PLAN_NETWORK_ERROR = 3,
    CODEX_PLAN_PARSE_ERROR   = 4,
    CODEX_PLAN_WRONG_TOKEN   = 5,
};

struct CodexPlanSnapshot {
    // Utilization percentages (0..100)
    float   session_pct;          // primary_window.used_percent (5h)
    float   weekly_pct;           // secondary_window.used_percent (7d)
    float   model_pct;            // highest additional per-model window, if present
    float   code_review_pct;      // code_review_rate_limit primary window, if present

    // Reset timestamps (UTC epoch seconds, 0 if unknown)
    time_t  session_resets_at;
    time_t  weekly_resets_at;
    time_t  model_resets_at;
    time_t  code_review_resets_at;

    // Labels copied into fixed buffers so the snapshot can be cached in NVS.
    char    plan_type[16];        // plus/pro/business/etc.
    char    model_name[28];       // limit_name for model_pct

    bool    has_model_limit;
    bool    has_code_review;

    // Bookkeeping
    time_t  last_updated;
    bool    valid;
    uint8_t error;
};
