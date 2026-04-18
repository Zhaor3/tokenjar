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

    // ── Today's cost ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?starting_at=%s&ending_at=%s&bucket_width=1d&limit=1",
            todayStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Today cost raw: %.400s\n", body.c_str());
            out.spend_today = parseCostResponse(body);
        } else {
            Serial.println("[CLAUDE] Today cost fetch FAILED");
        }
    }

    // ── Month cost ───────────────────────────────────────────────
    // limit=31 covers a full month; the default is small enough to truncate.
    // We also reuse the per-bucket daily totals to drive the sparkline so
    // it shows real variation instead of a flat line.
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
            Serial.printf("[CLAUDE] Month cost raw: %.200s...\n", body.c_str());
            out.spend_month = parseCostResponse(body);
            dailyCount = parseCostBuckets(body, dailyBuckets, SPARK_POINTS);
        }
    }

    // ── Today's tokens ───────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1d&limit=1",
            todayStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Today tokens raw: %.400s\n", body.c_str());
            out.tokens_today = parseTokenResponse(body);
            Serial.printf("[CLAUDE] tokens_today = %llu\n", (unsigned long long)out.tokens_today);
        }
    }

    // ── Month tokens ─────────────────────────────────────────────
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
            out.tokens_month = parseTokenResponse(body);
            Serial.printf("[CLAUDE] tokens_month = %llu\n", (unsigned long long)out.tokens_month);
        }
    }

    // Build sparkline from the per-day cost buckets we already fetched
    // for the month. Right-align so the newest day sits at the right edge;
    // leading slots are zero until there is enough history to fill.
    for (int i = 0; i < SPARK_POINTS; i++) out.hourly_spend[i] = 0;
    if (dailyCount > 0) {
        int copy = dailyCount < SPARK_POINTS ? dailyCount : SPARK_POINTS;
        for (int i = 0; i < copy; i++) {
            out.hourly_spend[SPARK_POINTS - copy + i] =
                dailyBuckets[dailyCount - copy + i];
        }
    }

    out.last_updated = time(nullptr);
    out.valid = gotData;
    Serial.printf("[CLAUDE] FINAL: today=$%.4f month=$%.4f tok_today=%llu tok_month=%llu valid=%d\n",
        out.spend_today, out.spend_month,
        (unsigned long long)out.tokens_today, (unsigned long long)out.tokens_month,
        out.valid);
    return out.valid;
}
