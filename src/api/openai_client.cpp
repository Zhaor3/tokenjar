#include "api/openai_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// OpenAI Admin Usage API.
// Docs: https://platform.openai.com/docs/api-reference/usage
// Costs endpoint expected shape:
// {
//   "data": [ { "results": [ { "amount": { "value": 1247 },
//               "line_item": "...", ... } ] } ],
//   "has_more": false
// }
// Completions usage expected shape:
// {
//   "data": [ { "results": [ { "input_tokens": 500000,
//               "output_tokens": 200000, ... } ] } ]
// }

// mktime() interprets struct tm as LOCAL time, so feeding it a gmtime_r-filled
// struct returns an epoch offset by the local UTC offset. Compute directly
// from the current epoch instead.
static time_t startOfDayUTC() {
    time_t now = time(nullptr);
    return now - (now % 86400);
}

static time_t startOfMonthUTC() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    time_t secs_into_month = ((time_t)(t.tm_mday - 1)) * 86400
                           + ((time_t)t.tm_hour) * 3600
                           + ((time_t)t.tm_min) * 60
                           + (time_t)t.tm_sec;
    return now - secs_into_month;
}

static bool oaiGet(const char* url, const char* apiKey, String& body) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    Serial.printf("[OPENAI] GET %s\n", url);
    if (!http.begin(client, url)) {
        Serial.println("[OPENAI] http.begin failed");
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + apiKey);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        body = http.getString();
        Serial.printf("[OPENAI] HTTP 200 — %d bytes\n", body.length());
        http.end();
        return true;
    }
    String errBody = http.getString();
    Serial.printf("[OPENAI] HTTP %d for %s\n", code, url);
    Serial.printf("[OPENAI] Error: %.200s\n", errBody.c_str());
    http.end();
    return false;
}

// Read one result row's `amount` as USD.
// `value` is a high-precision DECIMAL STRING (e.g. "0.917154150..."), not a
// number — ArduinoJson's `| 0.0f` fallback silently returns 0 if asked for
// a float. Some accounts may return `amount` as a bare number; handle both.
static float readAmountUSD(JsonObject r) {
    if (r["amount"].is<JsonObject>()) {
        auto v = r["amount"]["value"];
        if (v.is<const char*>()) return String(v.as<const char*>()).toFloat();
        return v | 0.0f;
    }
    if (r["amount"].is<const char*>()) {
        return String(r["amount"].as<const char*>()).toFloat();
    }
    return r["amount"] | 0.0f;
}

static float parseCosts(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[OPENAI] JSON parse failed (costs)");
        return 0;
    }

    float total = 0;
    int count = 0, buckets = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        buckets++;
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            total += readAmountUSD(r);
            count++;
        }
    }
    Serial.printf("[OPENAI] Parsed %d buckets, %d cost entries, total=$%.4f\n",
        buckets, count, total);
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
            sum += readAmountUSD(r);
        }
        out[n++] = sum;
    }
    return n;
}

static void parseTokens(const String& body, uint64_t& out) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;

    uint64_t sum = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            sum += (uint64_t)(r["input_tokens"] | 0) +
                   (uint64_t)(r["output_tokens"] | 0) +
                   (uint64_t)(r["input_cached_tokens"] | 0);
        }
    }
    out = sum;
}

bool OpenAIUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    // NOTE: do NOT memset `out` — the caller seeds it with the last known
    // snapshot so a transient subquery failure preserves previous values
    // instead of zeroing the screen. (Same bug as the Claude client had.)
    bool gotData = false;

    // Guard against querying before NTP sync — pre-sync epoch is near 0,
    // which produces start_time=0 and returns empty buckets.
    time_t now_check = time(nullptr);
    if (now_check < 1700000000) {
        Serial.printf("[OPENAI] Clock not synced (epoch=%ld) — skipping fetch\n",
            (long)now_check);
        return false;
    }

    time_t dayStart   = startOfDayUTC();
    time_t monthStart = startOfMonthUTC();
    Serial.printf("[OPENAI] day=%ld month=%ld now=%ld\n",
        (long)dayStart, (long)monthStart, (long)now_check);

    // ── Today's costs ────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/costs"
            "?start_time=%ld&bucket_width=1d&limit=1",
            (long)dayStart);

        String body;
        if (oaiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[OPENAI] Today cost raw: %.400s\n", body.c_str());
            out.spend_today = parseCosts(body);
        } else {
            Serial.println("[OPENAI] Today cost fetch FAILED");
        }
    }

    // ── Month costs ──────────────────────────────────────────────
    // We also reuse the per-day buckets from this response to drive the
    // sparkline so it shows real daily variation instead of a flat line.
    float dailyBuckets[SPARK_POINTS] = {0};
    int dailyCount = 0;
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/costs"
            "?start_time=%ld&bucket_width=1d&limit=31",
            (long)monthStart);

        String body;
        if (oaiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[OPENAI] Month cost raw: %.400s\n", body.c_str());
            out.spend_month = parseCosts(body);
            dailyCount = parseCostBuckets(body, dailyBuckets, SPARK_POINTS);
        } else {
            Serial.println("[OPENAI] Month cost fetch FAILED");
        }
    }

    // ── Today's tokens ───────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/usage/completions"
            "?start_time=%ld&bucket_width=1d&limit=1",
            (long)dayStart);

        String body;
        if (oaiGet(url, apiKey, body)) {
            gotData = true;
            parseTokens(body, out.tokens_today);
            Serial.printf("[OPENAI] tokens_today = %llu\n",
                (unsigned long long)out.tokens_today);
        }
    }

    // ── Month tokens ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/usage/completions"
            "?start_time=%ld&bucket_width=1d&limit=31",
            (long)monthStart);

        String body;
        if (oaiGet(url, apiKey, body)) {
            gotData = true;
            parseTokens(body, out.tokens_month);
            Serial.printf("[OPENAI] tokens_month = %llu\n",
                (unsigned long long)out.tokens_month);
        }
    }

    // Build sparkline from the per-day cost buckets we already fetched
    // for the month. Right-align so the newest day sits at the right edge.
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
    Serial.printf("[OPENAI] FINAL: today=$%.4f month=$%.4f tok_today=%llu tok_month=%llu valid=%d\n",
        out.spend_today, out.spend_month,
        (unsigned long long)out.tokens_today, (unsigned long long)out.tokens_month,
        out.valid);
    return out.valid;
}
