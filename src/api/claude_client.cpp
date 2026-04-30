#include "api/claude_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// Anthropic Admin API
// Docs: https://platform.claude.com/docs/en/api/admin-api/usage-cost
// Cost report: GET /v1/organizations/cost_report?starting_at=...&ending_at=...
//   Response: { "data": [ { "result": [ { "amount": "123.45", "currency": "USD", ... } ] } ] }
//   amount is a STRING in CENTS (lowest currency unit)
// Usage report: GET /v1/organizations/usage_report/messages?starting_at=...&ending_at=...
//   Response: { "data": [ { "result": [ { "uncached_input_tokens": N, "cache_creation": {...},
//               "output_tokens": N, ... } ] } ] }

// RFC 3339 date formatters (UTC)
static void todayStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    strftime(buf, len, "%Y-%m-%dT00:00:00Z", &t);
}

static void nowUTC(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &t);
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

static JsonArrayConst bucketResults(JsonObjectConst bucket) {
    JsonArrayConst arr = bucket["result"].as<JsonArrayConst>();
    if (arr.isNull()) arr = bucket["results"].as<JsonArrayConst>();
    return arr;
}

static float numOrStr(JsonVariantConst v) {
    if (v.isNull()) return 0.0f;
    if (v.is<float>() || v.is<double>()) return v.as<float>();
    if (v.is<int>() || v.is<long>()) return (float)v.as<long>();
    if (v.is<unsigned int>() || v.is<unsigned long>()) return (float)v.as<unsigned long>();
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return s ? (float)atof(s) : 0.0f;
    }
    return 0.0f;
}

static uint64_t uintOrZero(JsonVariantConst v) {
    if (v.isNull()) return 0;
    if (v.is<uint64_t>()) return v.as<uint64_t>();
    if (v.is<unsigned long>()) return (uint64_t)v.as<unsigned long>();
    if (v.is<long>()) {
        long n = v.as<long>();
        return n > 0 ? (uint64_t)n : 0;
    }
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return s ? strtoull(s, nullptr, 10) : 0;
    }
    return 0;
}

// Parse cost from data[].result[].amount (string in cents).
// If bucketStart is provided, also returns that bucket's total via bucketTotal.
static float parseCostResponse(const String& body,
                               const char* bucketStart = nullptr,
                               float* bucketTotal = nullptr) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[CLAUDE] JSON parse failed");
        return 0;
    }
    float total = 0;
    float bucketDollars = 0;
    int count = 0;
    for (JsonObjectConst bucket : doc["data"].as<JsonArrayConst>()) {
        const char* startingAt = bucket["starting_at"] | "";
        bool matchesBucket = bucketStart && strcmp(startingAt, bucketStart) == 0;
        JsonArrayConst rows = bucketResults(bucket);
        if (rows.isNull()) {
            Serial.println("[CLAUDE] Cost bucket missing result/results");
            continue;
        }
        for (JsonObjectConst r : rows) {
            // amount is a decimal string in cents, e.g. "123.45" = $1.2345
            JsonVariantConst amount = r["amount"];
            JsonObjectConst amountObj = amount.as<JsonObjectConst>();
            float cents = amountObj.isNull() ? numOrStr(amount) : numOrStr(amountObj["value"]);
            float dollars = cents / 100.0f;
            total += dollars;
            if (matchesBucket) bucketDollars += dollars;
            count++;
        }
    }
    if (bucketTotal) *bucketTotal = bucketDollars;
    Serial.printf("[CLAUDE] Parsed %d cost entries, total=$%.4f\n", count, total);
    return total;
}

// Estimate dollar cost from a usage_report response using Claude Sonnet 4
// pricing (per 1M tokens, USD): input $3, output $15, cache_read $0.30,
// cache_5m $3.75 (1.25x input), cache_1h $6 (2x input).
// Used for today's spend because cost_report excludes any in-progress day.
static float estimateSonnet4Cost(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return 0;

    double total = 0;
    for (JsonObjectConst bucket : doc["data"].as<JsonArrayConst>()) {
        JsonArrayConst rows = bucketResults(bucket);
        if (rows.isNull()) continue;
        for (JsonObjectConst r : rows) {
            total += (double)uintOrZero(r["uncached_input_tokens"]) * 3.00e-6;
            total += (double)uintOrZero(r["output_tokens"]) * 15.00e-6;
            total += (double)uintOrZero(r["cache_read_input_tokens"]) * 0.30e-6;
            total += (double)uintOrZero(r["cache_creation"]["ephemeral_5m_input_tokens"]) * 3.75e-6;
            total += (double)uintOrZero(r["cache_creation"]["ephemeral_1h_input_tokens"]) * 6.00e-6;
        }
    }
    return (float)total;
}

// Parse tokens from data[].result[].
static uint64_t parseTokenResponse(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[CLAUDE] JSON parse failed (tokens)");
        return 0;
    }
    uint64_t total = 0;
    int count = 0;
    for (JsonObjectConst bucket : doc["data"].as<JsonArrayConst>()) {
        JsonArrayConst rows = bucketResults(bucket);
        if (rows.isNull()) {
            Serial.println("[CLAUDE] Usage bucket missing result/results");
            continue;
        }
        for (JsonObjectConst r : rows) {
            uint64_t input = uintOrZero(r["uncached_input_tokens"]) +
                             uintOrZero(r["cache_read_input_tokens"]) +
                             uintOrZero(r["cache_creation_input_tokens"]) +
                             uintOrZero(r["cache_creation"]["ephemeral_1h_input_tokens"]) +
                             uintOrZero(r["cache_creation"]["ephemeral_5m_input_tokens"]);
            if (input == 0) input = uintOrZero(r["input_tokens"]);
            total += input + uintOrZero(r["output_tokens"]);
            count++;
        }
    }
    Serial.printf("[CLAUDE] Parsed %d token entries, total=%llu\n",
        count, (unsigned long long)total);
    return total;
}

bool ClaudeUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    UsageSnapshot prev = out;
    memset(&out, 0, sizeof(out));
    bool gotData = false;

    char todayStart[32], endAt[32], monthStart[32];
    todayStartUTC(todayStart, sizeof(todayStart));
    nowUTC(endAt, sizeof(endAt));
    monthStartUTC(monthStart, sizeof(monthStart));

    Serial.printf("[CLAUDE] Date range: today=%s end=%s month=%s\n",
        todayStart, endAt, monthStart);

    // ── Month cost (past days) ───────────────────────────────────
    // Anthropic cost_report only supports 1d buckets and excludes any
    // in-progress day. Today's spend is estimated from hourly token usage.
    bool gotMonthCost = false;
    float pastMonthCost = prev.spend_month - prev.spend_today;
    if (pastMonthCost < 0) pastMonthCost = prev.spend_month;
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?starting_at=%s&ending_at=%s&limit=31&group_by%%5B%%5D=description",
            monthStart, endAt);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            gotMonthCost = true;
            Serial.printf("[CLAUDE] Month cost raw: %.200s...\n", body.c_str());
            pastMonthCost = parseCostResponse(body);
            out.spend_today = prev.spend_today;
            out.spend_month = pastMonthCost + out.spend_today;
        } else {
            out.spend_today = prev.spend_today;
            out.spend_month = prev.spend_month;
        }
    }

    // ── Today's tokens ───────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1h&limit=24&group_by%%5B%%5D=model",
            todayStart, endAt);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Today tokens raw: %.400s\n", body.c_str());
            out.tokens_today = parseTokenResponse(body);
            out.spend_today = estimateSonnet4Cost(body);
            if (gotMonthCost) out.spend_month = pastMonthCost + out.spend_today;
            Serial.printf("[CLAUDE] tokens_today = %llu, est_cost = $%.4f (Sonnet 4)\n",
                (unsigned long long)out.tokens_today, out.spend_today);
        } else {
            out.tokens_today = prev.tokens_today;
        }
    }

    // ── Month tokens ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1d&limit=31&group_by%%5B%%5D=model",
            monthStart, endAt);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            out.tokens_month = parseTokenResponse(body) + out.tokens_today;
            Serial.printf("[CLAUDE] tokens_month = %llu\n", (unsigned long long)out.tokens_month);
        } else {
            out.tokens_month = prev.tokens_month;
        }
    }

    // Build sparkline (distribute daily cost across elapsed hours)
    time_t now = time(nullptr);
    struct tm tn;
    localtime_r(&now, &tn);
    int hours_today = tn.tm_hour + 1;
    float per_hour = (hours_today > 0) ? out.spend_today / hours_today : 0;
    for (int i = 0; i < SPARK_POINTS; i++)
        out.hourly_spend[i] = (i >= SPARK_POINTS - hours_today) ? per_hour : 0;

    out.last_updated = gotData ? time(nullptr) : prev.last_updated;
    out.valid = gotData || prev.valid;
    Serial.printf("[CLAUDE] FINAL: today=$%.4f month=$%.4f tok_today=%llu tok_month=%llu valid=%d\n",
        out.spend_today, out.spend_month,
        (unsigned long long)out.tokens_today, (unsigned long long)out.tokens_month,
        out.valid);
    return out.valid;
}
