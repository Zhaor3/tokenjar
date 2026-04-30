#include "api/openai_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

// OpenAI Admin Usage API.
// Docs: https://platform.openai.com/docs/api-reference/usage
// Costs endpoint expected shape:
// {
//   "data": [ { "result": [ { "amount": { "value": 12.47 },
//               "line_item": "...", ... } ] } ],
//   "has_more": false
// }
// Usage endpoint expected shape:
// {
//   "data": [ { "result": [ { "input_tokens": 500000,
//               "output_tokens": 200000, ... } ] } ]
// }

static int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static time_t epochUTC(int year, unsigned month, unsigned day,
                       unsigned hour, unsigned minute, unsigned second) {
    int64_t days = daysFromCivil(year, month, day);
    return (time_t)(days * 86400 + hour * 3600 + minute * 60 + second);
}

static time_t startOfDayUTC() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    return epochUTC(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, 0, 0, 0);
}

static time_t startOfMonthUTC() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    return epochUTC(t.tm_year + 1900, t.tm_mon + 1, 1, 0, 0, 0);
}

static bool oaiGet(const char* url, const char* apiKey, String& body) {
    for (int attempt = 1; attempt <= 2; ++attempt) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;

        Serial.printf("[OPENAI] GET %s%s\n", url, attempt > 1 ? " (retry)" : "");
        if (!http.begin(client, url)) {
            Serial.println("[OPENAI] http.begin failed");
            return false;
        }
        http.addHeader("Authorization", String("Bearer ") + apiKey);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(30000);

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

        if (code > 0) return false;
        delay(500);
    }
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

static float costAmount(JsonObjectConst r) {
    JsonVariantConst amount = r["amount"];
    JsonObjectConst amountObj = amount.as<JsonObjectConst>();
    if (!amountObj.isNull()) {
        return numOrStr(amountObj["value"]);
    }
    return numOrStr(amount);
}

static float parseCosts(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[OPENAI] JSON parse failed (costs)");
        return 0;
    }

    float total = 0;
    int count = 0;
    for (JsonObjectConst bucket : doc["data"].as<JsonArrayConst>()) {
        JsonArrayConst rows = bucketResults(bucket);
        if (rows.isNull()) {
            Serial.println("[OPENAI] Cost bucket missing result/results");
            continue;
        }
        for (JsonObjectConst r : rows) {
            float val = costAmount(r);
            total += val;
            if (val > 0) {
                Serial.printf("[OPENAI] cost entry: $%.4f\n", val);
            }
            count++;
        }
    }
    Serial.printf("[OPENAI] Parsed %d cost entries, total=$%.4f\n", count, total);
    return total;
}

static void parseTokens(const String& body, uint64_t& out) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[OPENAI] JSON parse failed (tokens)");
        return;
    }

    int count = 0;
    uint64_t before = out;
    for (JsonObjectConst bucket : doc["data"].as<JsonArrayConst>()) {
        JsonArrayConst rows = bucketResults(bucket);
        if (rows.isNull()) {
            Serial.println("[OPENAI] Usage bucket missing result/results");
            continue;
        }
        for (JsonObjectConst r : rows) {
            out += uintOrZero(r["input_tokens"]) +
                   uintOrZero(r["output_tokens"]);
            count++;
        }
    }
    Serial.printf("[OPENAI] Parsed %d token entries, added=%llu total=%llu\n",
        count,
        (unsigned long long)(out - before),
        (unsigned long long)out);
}

static bool fetchTokens(const char* apiKey,
                        const char* endpoint,
                        time_t start,
                        int limit,
                        bool hourly,
                        uint64_t& out) {
    char url[288];
    snprintf(url, sizeof(url),
        "https://api.openai.com/v1/organization/usage/%s"
        "?start_time=%ld&limit=%d%s",
        endpoint, (long)start, limit, hourly ? "&bucket_width=1h" : "");

    String body;
    if (oaiGet(url, apiKey, body)) {
        Serial.printf("[OPENAI] %s tokens raw: %.100s...\n", endpoint, body.c_str());
        parseTokens(body, out);
        return true;
    }
    return false;
}

bool OpenAIUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    UsageSnapshot prev = out;
    memset(&out, 0, sizeof(out));
    bool gotData = false;

    time_t dayStart   = startOfDayUTC();
    time_t monthStart = startOfMonthUTC();

    // ── Today's costs ────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/costs"
            "?start_time=%ld&limit=30",
            (long)dayStart);

        String body;
        if (oaiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[OPENAI] Today raw: %.100s...\n", body.c_str());
            out.spend_today = parseCosts(body);
            Serial.printf("[OPENAI] Today spend: $%.4f\n", out.spend_today);
        } else {
            Serial.println("[OPENAI] Today cost fetch FAILED");
            out.spend_today = prev.spend_today;
        }
    }

    // ── Month costs ──────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/costs"
            "?start_time=%ld&limit=31",
            (long)monthStart);

        String body;
        if (oaiGet(url, apiKey, body)) {
            gotData = true;
            out.spend_month = parseCosts(body);
        } else {
            out.spend_month = prev.spend_month;
        }
    }

    // ── Tokens ───────────────────────────────────────────────────
    // OpenAI reports several token-bearing products through separate
    // organization usage endpoints. Add the common text-token endpoints.
    bool gotTodayTokens = false;
    bool gotMonthTokens = false;
    gotTodayTokens |= fetchTokens(apiKey, "completions", dayStart,   24, true,  out.tokens_today);
    gotTodayTokens |= fetchTokens(apiKey, "embeddings",  dayStart,   24, true,  out.tokens_today);
    gotTodayTokens |= fetchTokens(apiKey, "moderations", dayStart,   24, true,  out.tokens_today);
    gotMonthTokens |= fetchTokens(apiKey, "completions", monthStart, 31, false, out.tokens_month);
    gotMonthTokens |= fetchTokens(apiKey, "embeddings",  monthStart, 31, false, out.tokens_month);
    gotMonthTokens |= fetchTokens(apiKey, "moderations", monthStart, 31, false, out.tokens_month);
    if (gotTodayTokens) gotData = true;
    else out.tokens_today = prev.tokens_today;
    if (gotMonthTokens) gotData = true;
    else out.tokens_month = prev.tokens_month;

    // Build sparkline from daily spend distributed across elapsed hours
    time_t now = time(nullptr);
    struct tm tn;
    localtime_r(&now, &tn);
    int hours_today = tn.tm_hour + 1;
    float per_hour = (hours_today > 0) ? out.spend_today / hours_today : 0;
    for (int i = 0; i < SPARK_POINTS; i++)
        out.hourly_spend[i] = (i >= SPARK_POINTS - hours_today) ? per_hour : 0;

    out.last_updated = gotData ? now : prev.last_updated;
    out.valid = gotData || prev.valid;
    return out.valid;
}
