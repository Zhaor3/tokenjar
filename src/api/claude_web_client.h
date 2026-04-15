#pragma once
#include <Arduino.h>
#include "api/claude_plan.h"

// ClaudeWebClient — scrapes claude.ai's *undocumented* subscription
// usage endpoints using a sessionKey cookie (sk-ant-sid01-...).
//
// This is NOT the Admin API. There is no official/supported endpoint
// for this data as of April 2026. We're replicating what open-source
// community tools (Claude-Usage-Tracker, Usage4Claude, claude-quota-tracker)
// have reverse-engineered.
//
// Call `fetch()` at most once per 5 minutes — claude.ai rate-limits
// aggressively and we don't want to get IP-banned.
//
// If the cached org UUID is empty, this will first hit
// GET https://claude.ai/api/organizations to discover it, then cache
// it via SettingsStore. Pass an empty string on first call.
//
// The returned `out.error` code tells the caller which failure mode
// triggered if `out.valid` is false (so the UI can show an actionable
// message like "AUTH EXPIRED — RE-PASTE COOKIE").

class ClaudeWebClient {
public:
    // Top-level fetch. If `cachedOrgId` is empty, it fetches+stores one
    // via the (non-owning) `outOrgId` buffer. On success, fills `out`
    // with plan usage data.
    //
    // Returns true if `out.valid` was set true (i.e. /usage succeeded).
    // Overage endpoint failure does NOT flip valid to false — it just
    // leaves `extra_enabled` as false.
    bool fetch(const char* sessionKey,
               const char* cachedOrgId,
               ClaudePlanSnapshot& out,
               String& outOrgId);

private:
    // Steps 1–3 of the scrape flow.
    bool fetchOrgId(const char* sessionKey, String& outOrgId, uint8_t& errOut);
    bool fetchUsage(const char* sessionKey,
                    const char* orgId,
                    ClaudePlanSnapshot& out);
    bool fetchOverage(const char* sessionKey,
                      const char* orgId,
                      ClaudePlanSnapshot& out);

    // HTTP helper — applies the full browser-mimicking header set.
    // Returns the HTTP status code, or -1 on transport failure.
    // On any 2xx, `body` is populated; on 4xx/5xx it still tries to
    // populate `body` so the caller can tell HTML (CF challenge) from
    // JSON (real error).
    int getWithCookie(const char* url,
                      const char* sessionKey,
                      String& body);

    // Parses an ISO 8601 UTC timestamp like "2026-04-15T23:00:00.000000+00:00"
    // into a unix epoch (seconds). Returns 0 on parse failure.
    static time_t parseIso8601Utc(const char* iso);
};
