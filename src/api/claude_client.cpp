#include "api/claude_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Anthropic Admin API
// Docs: https://platform.claude.com/docs/en/api/admin-api/usage-cost
// Cost report: GET /v1/organizations/cost_report?starting_at=...&ending_at=...
//   Response: { "data": [ { "results": [ { "amount": "123.45", "currency": "USD", ... } ] } ] }
//   amount is a STRING in CENTS (lowest currency unit)
// Usage report: GET /v1/organizations/usage_report/messages?starting_at=...&ending_at=...
//   Response: { "data": [ { "results": [ { "uncached_input_tokens": N, "output_tokens": N, ... } ] } ] }

// RFC 3339 date formatters (UTC)
static void todayStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    strftime(buf, len, "%Y-%m-%dT00:00:00Z", &t);
}

static void tomorrowStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr) + 86400;
    struct tm t;
    gmtime_r(&now, &t);
    snprintf(buf, len, "%04d-%02d-%02dT00:00:00Z", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

static void monthStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    snprintf(buf, len, "%04d-%02d-01T00:00:00Z", t.tm_year + 1900, t.tm_mon + 1);
}

static bool apiGet(const char* url, const char* apiKey, String& body) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    Serial.printf("[CLAUDE] GET %s\n", url);
    if (!http.begin(client, url)) {
        Serial.println("[CLAUDE] http.begin failed");
        return false;
    }
    http.addHeader("x-api-key", apiKey);
    http.addHeader("anthropic-version", "2023-06-01");
    http.setTimeout(15000);

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        body = http.getString();
        Serial.printf("[CLAUDE] HTTP 200 — %d bytes\n", body.length());
        http.end();
        return true;
    }
    // Print error body for debugging
    String errBody = http.getString();
    Serial.printf("[CLAUDE] HTTP %d for %s\n", code, url);
    Serial.printf("[CLAUDE] Error: %.200s\n", errBody.c_str());
    http.end();
    return false;
}

// Read one result row's `amount` as dollars.
// The Anthropic cost_report returns `amount` as a decimal string in CENTS.
static float readAmountDollars(JsonObject r) {
    float cents = 0;
    if (r["amount"].is<const char*>()) {
        cents = String(r["amount"].as<const char*>()).toFloat();
    } else {
        cents = r["amount"].as<float>();
    }
    return cents / 100.0f;
}

// Parse cost from data[].results[].amount (string in cents)
static float parseCostResponse(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[CLAUDE] JSON parse failed");
        return 0;
    }
    float total = 0;
    int count = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            total += readAmountDollars(r);
            count++;
        }
    }
    Serial.printf("[CLAUDE] Parsed %d cost entries, total=$%.4f\n", count, total);
    return total;
}

// Extract per-bucket cost totals (chronological, oldest → newest) for
// the sparkline. Returns the number of buckets written into `out`.
static int parseCostBuckets(const String& body, float* out, int maxBuckets) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return 0;
    int n = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        if (n >= maxBuckets) break;
        float sum = 0;
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            sum += readAmountDollars(r);
        }
        out[n++] = sum;
    }
    return n;
}

// Estimate dollar cost from a usage_report response using Claude Sonnet 4
// pricing (per 1M tokens, USD): input $3, output $15, cache_read $0.30,
// cache_5m $3.75 (1.25× input), cache_1h $6 (2× input).
// Used for today's spend because cost_report excludes any in-progress day.
// Locked to Sonnet 4 — heavy Opus usage will under-estimate.
static float estimateSonnet4Cost(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return 0;
    double total = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            JsonObject cc = r["cache_creation"];
            total += (double)(uint64_t)(r["uncached_input_tokens"]      | 0) * 3.00e-6;
            total += (double)(uint64_t)(r["output_tokens"]              | 0) * 15.00e-6;
            total += (double)(uint64_t)(r["cache_read_input_tokens"]    | 0) * 0.30e-6;
            total += (double)(uint64_t)(cc["ephemeral_5m_input_tokens"] | 0) * 3.75e-6;
            total += (double)(uint64_t)(cc["ephemeral_1h_input_tokens"] | 0) * 6.00e-6;
        }
    }
    return (float)total;
}

// Parse tokens from data[].results[].
// cache_creation is a nested object ({ephemeral_1h_input_tokens, ephemeral_5m_input_tokens}).
// Leaving it out undercounts caching-heavy workloads like Claude Code.
static uint64_t parseTokenResponse(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[CLAUDE] JSON parse failed (tokens)");
        return 0;
    }
    uint64_t total = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            JsonObject cc = r["cache_creation"];
            total += (uint64_t)(r["uncached_input_tokens"] | 0) +
                     (uint64_t)(r["output_tokens"] | 0) +
                     (uint64_t)(r["cache_read_input_tokens"] | 0) +
                     (uint64_t)(cc["ephemeral_5m_input_tokens"] | 0) +
                     (uint64_t)(cc["ephemeral_1h_input_tokens"] | 0);
        }
    }
    return total;
}

bool ClaudeUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    // NOTE: don't memset `out` here — the caller passes in the last known
    // snapshot so that a transient failure on one of the four subqueries
    // (e.g. today's cost) doesn't clobber the good values from a previous
    // refresh with zeros.
    bool gotData = false;

    // Guard against querying the API before NTP sync completes. Without a
    // real clock, starting_at ends up in 1970, which returns no data and
    // silently cached zeros for the day.
    time_t now_check = time(nullptr);
    if (now_check < 1700000000) {   // ~2023-11-14; pre-NTP time is near 0
        Serial.printf("[CLAUDE] Clock not synced (epoch=%ld) — skipping fetch\n",
            (long)now_check);
        return false;
    }

    char todayStart[32], tomorrowStart[32], monthStart[32];
    todayStartUTC(todayStart, sizeof(todayStart));
    tomorrowStartUTC(tomorrowStart, sizeof(tomorrowStart));
    monthStartUTC(monthStart, sizeof(monthStart));

    Serial.printf("[CLAUDE] Date range: today=%s tomorrow=%s month=%s\n",
        todayStart, tomorrowStart, monthStart);

    // Anthropic's cost_report ONLY supports bucket_width=1d (1h returns HTTP
    // 400: "Input should be '1d'"), and 1d buckets exclude any in-progress
    // day. So today's exact cost is unavailable from the Admin API. We
    // estimate it from today's per-token-type usage using Sonnet 4 pricing
    // (see estimateSonnet4Cost). Catches up exactly tomorrow when today
    // rolls into past_days.

    // ── Month cost (past days only) ──────────────────────────────
    // Per-day buckets also drive the sparkline.
    float past_days_cost = 0;
    bool got_past_cost = false;
    float dailyBuckets[SPARK_POINTS] = {0};
    int dailyCount = 0;
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?starting_at=%s&ending_at=%s&bucket_width=1d&limit=31",
            monthStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            got_past_cost = true;
            Serial.printf("[CLAUDE] Month cost raw: %.200s...\n", body.c_str());
            past_days_cost = parseCostResponse(body);
            dailyCount = parseCostBuckets(body, dailyBuckets, SPARK_POINTS);
        }
    }

    // ── Today's tokens + cost estimate ───────────────────────────
    // 1h buckets work for usage_report (unlike cost_report) and capture
    // the in-progress day. We also compute a Sonnet-4 priced cost estimate
    // from the same response since cost_report won't return today.
    uint64_t today_tokens = out.tokens_today;   // seed with cached value
    float    today_cost   = out.spend_today;    // seed with cached value
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1h&limit=24",
            todayStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Today tokens raw: %.400s\n", body.c_str());
            today_tokens = parseTokenResponse(body);
            today_cost   = estimateSonnet4Cost(body);
            out.tokens_today = today_tokens;
            out.spend_today  = today_cost;
            Serial.printf("[CLAUDE] tokens_today = %llu, est_cost = $%.4f (Sonnet 4)\n",
                (unsigned long long)out.tokens_today, today_cost);
        }
    }

    // ── Month tokens (past days) ─────────────────────────────────
    // Without limit, usage_report defaults to 7 buckets for bucket_width=1d,
    // silently truncating anything past day 7 of the month.
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1d&limit=31",
            monthStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            uint64_t past_days_tokens = parseTokenResponse(body);
            out.tokens_month = past_days_tokens + today_tokens;
            Serial.printf("[CLAUDE] tokens_month = %llu (past=%llu + today=%llu)\n",
                (unsigned long long)out.tokens_month,
                (unsigned long long)past_days_tokens,
                (unsigned long long)today_tokens);
        }
    }

    // ── Combine month total ──────────────────────────────────────
    // past_days_cost from cost_report (real, completed days) + today_cost
    // estimated from Sonnet 4 token pricing.
    if (got_past_cost) {
        out.spend_month = past_days_cost + today_cost;
        Serial.printf("[CLAUDE] Month: past_days=$%.4f + today_est=$%.4f = $%.4f\n",
            past_days_cost, today_cost, out.spend_month);
    }

    // Build sparkline from the per-day cost buckets we already fetched
    // for the month. Right-align so the newest day sits at the right edge;
    // leading slots are zero until there is enough history to fill.
    // Overlay today's actual cost on the last slot — the 1d response either
    // omits today or returns an empty bucket for it.
    for (int i = 0; i < SPARK_POINTS; i++) out.hourly_spend[i] = 0;
    if (dailyCount > 0) {
        int copy = dailyCount < SPARK_POINTS ? dailyCount : SPARK_POINTS;
        for (int i = 0; i < copy; i++) {
            out.hourly_spend[SPARK_POINTS - copy + i] =
                dailyBuckets[dailyCount - copy + i];
        }
    }
    out.hourly_spend[SPARK_POINTS - 1] = today_cost;

    out.last_updated = time(nullptr);
    out.valid = gotData;
    Serial.printf("[CLAUDE] FINAL: today=$%.4f month=$%.4f tok_today=%llu tok_month=%llu valid=%d\n",
        out.spend_today, out.spend_month,
        (unsigned long long)out.tokens_today, (unsigned long long)out.tokens_month,
        out.valid);
    return out.valid;
}
