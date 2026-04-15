#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "config.h"
#include "theme.h"
#include "storage/settings_store.h"
#include "api/usage_provider.h"
#include "api/claude_plan.h"
#include "api/claude_client.h"
#include "api/claude_web_client.h"
#include "api/openai_client.h"
#include "input/encoder.h"
#include "ui/ui_manager.h"

// ── Globals ──────────────────────────────────────────────────────

static TFT_eSPI        tft;
static SettingsStore    store;
static Encoder          enc;
static ClaudeUsageClient claudeApi;
static OpenAIUsageClient openaiApi;
static ClaudeWebClient   claudeWebApi;
static UIManager        ui;

// Shared data between main loop and API task
static SemaphoreHandle_t dataMtx;
static UsageSnapshot     claudeSnap, openaiSnap;
static ClaudePlanSnapshot planSnap;
static TaskHandle_t      apiTask = nullptr;
static volatile bool     dataReady = false;
static volatile bool     planReady = false;

// Claude.ai endpoint has its own (slower) refresh cadence — 5 minutes.
// The API task wakes every API_REFRESH_MS (60s) but only hits claude.ai
// if enough time has elapsed since the last plan fetch.
static constexpr uint32_t PLAN_REFRESH_MS = 5UL * 60UL * 1000UL;
static uint32_t last_plan_fetch_ms = 0;

// Portal mode
static WebServer*  portal_server = nullptr;
static DNSServer*  portal_dns    = nullptr;

// Display buffer — sized for the larger dimension so it fits either orientation
static uint8_t lvBuf[SCREEN_MAX_DIM * 20 * sizeof(lv_color_t)];

// ── App state machine ────────────────────────────────────────────

enum class State { SPLASH, ORIENTATION, PORTAL, CONNECTING, RUNNING };
static State state = State::SPLASH;
static uint32_t stateStart = 0;

// Orientation chosen at boot (horizontal is the new default)
static bool g_horizontal   = true;
static bool g_needOrientPrompt = false;

// ── LVGL display flush ──────────────────────────────────────────

static void dispFlush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)px, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(d);
}

static uint32_t lvTick() { return millis(); }

// ── API refresh task (core 0) ───────────────────────────────────

static void apiRefreshLoop(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(API_REFRESH_MS));

        // ── Admin API fetches (every wake-up, ~60s) ──────────────
        UsageSnapshot c = {}, o = {};

        String ck = store.claudeKey();
        if (ck.length() > 0)
            claudeApi.fetch(ck.c_str(), c);

        String ok_ = store.openaiKey();
        if (ok_.length() > 0)
            openaiApi.fetch(ok_.c_str(), o);

        if (xSemaphoreTake(dataMtx, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (c.valid) { claudeSnap = c; store.saveCache("claude", c); }
            if (o.valid) { openaiSnap = o; store.saveCache("openai", o); }
            dataReady = true;
            xSemaphoreGive(dataMtx);
        }

        // ── Claude.ai subscription plan (rate-limited to 5 min) ──
        String sid = store.claudeSession();
        uint32_t now_ms = millis();
        bool first_fetch = (last_plan_fetch_ms == 0);
        bool interval_due = (now_ms - last_plan_fetch_ms) >= PLAN_REFRESH_MS;

        if (sid.length() > 0 && (first_fetch || interval_due)) {
            last_plan_fetch_ms = now_ms;

            // Work against a local copy so a failed fetch doesn't wipe
            // cached values in planSnap.
            ClaudePlanSnapshot p;
            if (xSemaphoreTake(dataMtx, pdMS_TO_TICKS(500)) == pdTRUE) {
                p = planSnap;
                xSemaphoreGive(dataMtx);
            } else {
                memset(&p, 0, sizeof(p));
            }

            String orgId = store.claudeOrgId();
            String newOrgId = orgId;
            claudeWebApi.fetch(sid.c_str(), orgId.c_str(), p, newOrgId);

            // Persist any newly discovered org UUID.
            if (newOrgId.length() > 0 && newOrgId != orgId) {
                store.setClaudeOrgId(newOrgId);
            } else if (newOrgId.length() == 0 && orgId.length() > 0) {
                // fetch() clears newOrgId on auth failure — force rediscovery
                store.clearClaudeOrgId();
            }

            if (xSemaphoreTake(dataMtx, pdMS_TO_TICKS(500)) == pdTRUE) {
                planSnap = p;
                if (p.valid) store.savePlanCache(p);
                planReady = true;
                xSemaphoreGive(dataMtx);
            }
        }
    }
}

// ── Captive-portal HTML (embedded) ───────────────────────────────

static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TOKENJAR Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#f5f1ea;font-family:'JetBrains Mono',monospace;padding:24px}
h1{font-size:14px;letter-spacing:4px;color:#d97757;margin-bottom:24px}
label{display:block;font-size:11px;letter-spacing:2px;color:#6b6660;margin:16px 0 4px}
input,select{width:100%;padding:10px;background:#111;border:1px solid #1a1a1a;color:#f5f1ea;font-size:13px;font-family:inherit}
input:focus{border-color:#d97757;outline:none}
button{width:100%;padding:12px;margin-top:20px;background:#d97757;color:#000;border:none;font-size:12px;letter-spacing:2px;cursor:pointer;font-family:inherit}
button:active{opacity:.8}
.row{display:flex;gap:12px}
.row>*{flex:1}
.result{font-size:11px;margin-top:8px;color:#6b6660}
.ok{color:#4ade80}.fail{color:#ef4444}
a{color:#d97757}
hr{border:0;border-top:1px solid #1a1a1a;margin:20px 0}
#networks{max-height:150px;overflow-y:auto;border:1px solid #1a1a1a;margin-bottom:8px}
#networks div{padding:8px 10px;cursor:pointer;font-size:12px}
#networks div:hover{background:#111}
#networks div.sel{background:#1a1a1a;color:#d97757}
</style></head><body>
<h1>TOKENJAR</h1>
<label>WIFI NETWORK</label>
<input type="text" id="wssid" placeholder="Type your WiFi name here">
<div id="networks" style="margin-top:8px">Scanning...</div>
<label>WIFI PASSWORD</label>
<input type="password" id="wpass">
<hr>
<label>ANTHROPIC ADMIN API KEY <a href="https://console.anthropic.com/settings/admin-keys" target="_blank">(how to get this)</a></label>
<input type="text" id="ckey" placeholder="sk-ant-admin...">
<div class="row">
  <div><label>DAILY BUDGET ($)</label><input type="number" id="cbd" value="5" step="0.5"></div>
  <div><label>MONTHLY BUDGET ($)</label><input type="number" id="cbm" value="100" step="1"></div>
</div>
<label>CLAUDE.AI SESSION COOKIE (optional - Pro/Max plan tracking)</label>
<input type="text" id="csid" placeholder="sk-ant-sid01-...">
<hr>
<label>OPENAI ADMIN API KEY <a href="https://platform.openai.com/settings/organization/admin-keys" target="_blank">(how to get this)</a></label>
<input type="text" id="okey" placeholder="sk-admin-...">
<div class="row">
  <div><label>DAILY BUDGET ($)</label><input type="number" id="obd" value="5" step="0.5"></div>
  <div><label>MONTHLY BUDGET ($)</label><input type="number" id="obm" value="100" step="1"></div>
</div>
<hr>
<label>TIMEZONE (POSIX)</label>
<input type="text" id="tz" value="EST5EDT,M3.2.0,M11.1.0">
<label>OTA PASSWORD (optional)</label>
<input type="password" id="ota">
<button onclick="testApis()">TEST CONNECTIONS</button>
<div id="tres" class="result"></div>
<button onclick="saveConfig()">SAVE &amp; REBOOT</button>
<div id="sres" class="result"></div>
<script>
function doScan(tries){
  fetch('/scan').then(r=>r.json()).then(d=>{
    let el=document.getElementById('networks');
    if(!d.length&&tries>0){el.textContent='Scanning... ('+tries+')';setTimeout(()=>doScan(tries-1),3000);return;}
    if(!d.length){el.textContent='No networks found. Type your SSID above.';return;}
    el.innerHTML='';
    d.forEach(n=>{let div=document.createElement('div');
      div.textContent=n.ssid+' ('+n.rssi+'dBm)';
      div.onclick=()=>{document.querySelectorAll('#networks div').forEach(x=>x.classList.remove('sel'));
        div.classList.add('sel');document.getElementById('wssid').value=n.ssid;};
      el.appendChild(div);});
  }).catch(()=>{document.getElementById('networks').textContent='Scan failed. Type SSID above.';});
}
doScan(5);
function testApis(){
  let r=document.getElementById('tres');r.textContent='Testing...';r.className='result';
  fetch('/test',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ckey:document.getElementById('ckey').value,
      okey:document.getElementById('okey').value})})
  .then(x=>x.json()).then(j=>{
    let msg='';
    if(j.claude!==undefined) msg+='Claude: '+(j.claude?'OK':'FAIL')+' ';
    if(j.openai!==undefined) msg+='OpenAI: '+(j.openai?'OK':'FAIL');
    r.textContent=msg;r.className='result '+(msg.includes('FAIL')?'fail':'ok');
  });
}
function saveConfig(){
  let r=document.getElementById('sres');r.textContent='Saving...';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:document.getElementById('wssid').value,pass:document.getElementById('wpass').value,
      ckey:document.getElementById('ckey').value,
      csid:document.getElementById('csid').value,
      okey:document.getElementById('okey').value,
      cbd:parseFloat(document.getElementById('cbd').value),
      cbm:parseFloat(document.getElementById('cbm').value),
      obd:parseFloat(document.getElementById('obd').value),
      obm:parseFloat(document.getElementById('obm').value),
      tz:document.getElementById('tz').value,
      ota:document.getElementById('ota').value})})
  .then(x=>x.json()).then(j=>{r.textContent=j.ok?'Saved! Rebooting...':'Error';
    r.className='result '+(j.ok?'ok':'fail');});
}
</script></body></html>
)rawliteral";

// ── Portal handlers ──────────────────────────────────────────────

static void portalSetup() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    // Explicit AP config — ensure IP is 192.168.4.1
    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.softAP(AP_SSID);
    delay(1000);  // Give AP time to fully start

    Serial.printf("[PORTAL] AP started: SSID=%s  IP=%s\n",
        AP_SSID, WiFi.softAPIP().toString().c_str());

    portal_dns = new DNSServer();
    portal_dns->start(53, "*", WiFi.softAPIP());

    portal_server = new WebServer(80);

    portal_server->on("/", HTTP_GET, []() {
        portal_server->send_P(200, "text/html", PORTAL_HTML);
    });

    portal_server->on("/ping", HTTP_GET, []() {
        Serial.println("[PORTAL] /ping hit!");
        portal_server->send(200, "text/plain", "TOKENJAR OK");
    });

    // Start a background scan immediately
    WiFi.scanNetworks(true);  // async=true

    portal_server->on("/scan", HTTP_GET, []() {
        Serial.println("[PORTAL] /scan hit");
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            portal_server->send(200, "application/json", "[]");
            return;
        }
        if (n == WIFI_SCAN_FAILED || n < 0) {
            WiFi.scanNetworks(true);  // retry async
            portal_server->send(200, "application/json", "[]");
            return;
        }
        String json = "[";
        for (int i = 0; i < n; i++) {
            if (i) json += ",";
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        json += "]";
        portal_server->send(200, "application/json", json);
        WiFi.scanDelete();
        WiFi.scanNetworks(true);  // start next scan
    });

    portal_server->on("/test", HTTP_POST, []() {
        String body = portal_server->arg("plain");
        JsonDocument doc;
        deserializeJson(doc, body);
        String json = "{";
        String ck = doc["ckey"] | "";
        String ok = doc["okey"] | "";
        bool first = true;
        if (ck.length() > 0) {
            UsageSnapshot s;
            bool r = claudeApi.fetch(ck.c_str(), s);
            json += "\"claude\":" + String(r ? "true" : "false");
            first = false;
        }
        if (ok.length() > 0) {
            UsageSnapshot s;
            bool r = openaiApi.fetch(ok.c_str(), s);
            if (!first) json += ",";
            json += "\"openai\":" + String(r ? "true" : "false");
        }
        json += "}";
        portal_server->send(200, "application/json", json);
    });

    portal_server->on("/save", HTTP_POST, []() {
        String body = portal_server->arg("plain");
        JsonDocument doc;
        deserializeJson(doc, body);

        String ssid = doc["ssid"] | "";
        String pass = doc["pass"] | "";
        if (ssid.length() > 0) store.setWiFi(ssid, pass);

        String ck = doc["ckey"] | "";
        if (ck.length() > 0) store.setClaudeKey(ck);

        // Optional Claude.ai session cookie (for Pro/Max plan scraping).
        // setClaudeSession() internally invalidates the cached org UUID
        // whenever the session value actually changes.
        String csid = doc["csid"] | "";
        if (csid.length() > 0) store.setClaudeSession(csid);

        String ok = doc["okey"] | "";
        if (ok.length() > 0) store.setOpenAIKey(ok);

        store.setClaudeBudget(doc["cbd"] | DEFAULT_CLAUDE_DAILY_BUDGET,
                              doc["cbm"] | DEFAULT_CLAUDE_MONTHLY_BUDGET);
        store.setOpenAIBudget(doc["obd"] | DEFAULT_OPENAI_DAILY_BUDGET,
                              doc["obm"] | DEFAULT_OPENAI_MONTHLY_BUDGET);

        String tz = doc["tz"] | DEFAULT_TZ;
        store.setTimezone(tz);

        String ota = doc["ota"] | "";
        if (ota.length() > 0) store.setOTAPass(ota);

        portal_server->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    // Captive portal catch-all
    portal_server->onNotFound([]() {
        portal_server->sendHeader("Location", "http://192.168.4.1/", true);
        portal_server->send(302, "text/plain", "");
    });

    portal_server->begin();
}

static void portalTeardown() {
    if (portal_server) { portal_server->stop(); delete portal_server; portal_server = nullptr; }
    if (portal_dns)    { portal_dns->stop();    delete portal_dns;    portal_dns = nullptr; }
}

// ── Setup ────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== TOKENJAR v1.0 ===");

    // Backlight — simple digital on (PWM can be added later)
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    // NVS first — we need orientation preference before TFT/LVGL init
    store.begin();

    if (store.hasOrientation()) {
        g_horizontal = store.orientationHorizontal();
    } else {
        g_horizontal       = true;   // default to horizontal on first boot
        g_needOrientPrompt = true;   // ask user after splash
    }
    Serial.printf("Orientation: %s%s\n",
        g_horizontal ? "horizontal" : "vertical",
        g_needOrientPrompt ? " (first-boot default; will prompt)" : "");

    // TFT — rotation 1 = landscape, rotation 0 = portrait
    tft.init();
    tft.setRotation(g_horizontal ? 1 : 0);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    // LVGL — size the display surface to match the active orientation
    lv_init();
    lv_tick_set_cb(lvTick);
    uint16_t disp_w = g_horizontal ? SCREEN_W_H : SCREEN_W;
    uint16_t disp_h = g_horizontal ? SCREEN_H_H : SCREEN_H;
    lv_display_t* disp = lv_display_create(disp_w, disp_h);
    lv_display_set_flush_cb(disp, dispFlush);
    lv_display_set_buffers(disp, lvBuf, nullptr, sizeof(lvBuf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Encoder
    enc.begin();

    // Splash
    lv_obj_t* splash = UIManager::makeSplash();
    lv_screen_load(splash);
    state = State::SPLASH;
    stateStart = millis();

    Serial.println("Boot OK — splash shown");
}

// ── Loop ─────────────────────────────────────────────────────────

void loop() {
    lv_timer_handler();
    enc.update();

    switch (state) {

    // ── SPLASH ───────────────────────────────────────────────────
    case State::SPLASH:
        // Long-press during splash → force portal mode
        if (enc.wasLongPressed()) {
            Serial.println("Long-press detected — entering setup portal");
            store.clear();
            g_needOrientPrompt = true;   // re-ask after factory reset
        }
        if (millis() - stateStart >= SPLASH_MS) {
            if (g_needOrientPrompt) {
                lv_obj_t* orient = UIManager::makeOrientationChoice();
                lv_screen_load(orient);
                state = State::ORIENTATION;
                Serial.println("First boot / reset — prompting for orientation");
            } else if (!store.hasWiFi()) {
                portalSetup();
                lv_obj_t* qr = UIManager::makeQRSetup();
                lv_screen_load(qr);
                state = State::PORTAL;
                Serial.println("No WiFi configured — portal mode");
            } else {
                WiFi.mode(WIFI_STA);
                WiFi.begin(store.ssid().c_str(), store.pass().c_str());
                lv_obj_t* conn = UIManager::makeConnecting(store.ssid().c_str());
                lv_screen_load(conn);
                state = State::CONNECTING;
                stateStart = millis();
                Serial.printf("Connecting to %s...\n", store.ssid().c_str());
            }
        }
        break;

    // ── ORIENTATION CHOICE (first boot) ─────────────────────────
    case State::ORIENTATION: {
        // Turn encoder → toggle selection
        int rot = enc.rotation();
        if (rot != 0) {
            bool cur = UIManager::orientationChoiceGetSel();
            UIManager::orientationChoiceSetSel(!cur);
        }
        // Press encoder → commit
        if (enc.wasPressed()) {
            bool chosen = UIManager::orientationChoiceGetSel();
            store.setOrientation(chosen ? "horizontal" : "vertical");
            g_needOrientPrompt = false;
            Serial.printf("Orientation saved: %s\n",
                chosen ? "horizontal" : "vertical");

            // If user picked a different orientation than the one currently
            // applied to TFT/LVGL, we must reboot so the new rotation and
            // display dimensions take effect cleanly.
            if (chosen != g_horizontal) {
                Serial.println("Orientation changed — rebooting...");
                delay(500);
                ESP.restart();
            }

            // Same orientation — proceed to the normal boot path
            if (!store.hasWiFi()) {
                portalSetup();
                lv_obj_t* qr = UIManager::makeQRSetup();
                lv_screen_load(qr);
                state = State::PORTAL;
                Serial.println("No WiFi configured — portal mode");
            } else {
                WiFi.mode(WIFI_STA);
                WiFi.begin(store.ssid().c_str(), store.pass().c_str());
                lv_obj_t* conn = UIManager::makeConnecting(store.ssid().c_str());
                lv_screen_load(conn);
                state = State::CONNECTING;
                stateStart = millis();
                Serial.printf("Connecting to %s...\n", store.ssid().c_str());
            }
        }
        break;
    }

    // ── CAPTIVE PORTAL ───────────────────────────────────────────
    case State::PORTAL: {
        portal_dns->processNextRequest();
        portal_server->handleClient();
        static uint32_t lastDbg = 0;
        if (millis() - lastDbg > 5000) {
            lastDbg = millis();
            Serial.printf("[PORTAL] AP IP=%s  clients=%d  heap=%d\n",
                WiFi.softAPIP().toString().c_str(),
                WiFi.softAPgetStationNum(),
                ESP.getFreeHeap());
        }
        break;
    }

    // ── CONNECTING ───────────────────────────────────────────────
    case State::CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());

            // Time sync
            configTzTime(store.timezone().c_str(), NTP_SERVER);

            // mDNS
            MDNS.begin(MDNS_HOST);

            // OTA
            ArduinoOTA.setHostname(MDNS_HOST);
            if (store.hasOTAPass())
                ArduinoOTA.setPassword(store.otaPass().c_str());
            ArduinoOTA.begin();

            // Load cached snapshots
            store.loadCache("claude", claudeSnap);
            store.loadCache("openai", openaiSnap);
            store.loadPlanCache(planSnap);

            // Init UI (builds mode ring from configured keys)
            ui.init(store, g_horizontal);
            ui.updateData(claudeSnap, openaiSnap, store);
            ui.updatePlan(planSnap);

            // Start API refresh task on core 0
            dataMtx = xSemaphoreCreateMutex();
            xTaskCreatePinnedToCore(apiRefreshLoop, "api", 8192,
                                    nullptr, 1, &apiTask, 0);
            xTaskNotifyGive(apiTask);   // trigger first fetch

            state = State::RUNNING;
            Serial.println("Running — UI active");
        } else if (millis() - stateStart > WIFI_CONNECT_TIMEOUT) {
            Serial.println("WiFi timeout — using cached data");
            store.loadCache("claude", claudeSnap);
            store.loadCache("openai", openaiSnap);
            store.loadPlanCache(planSnap);
            ui.init(store, g_horizontal);
            ui.updateData(claudeSnap, openaiSnap, store);
            ui.updatePlan(planSnap);
            state = State::RUNNING;
        }
        break;

    // ── RUNNING ──────────────────────────────────────────────────
    case State::RUNNING: {
        // Encoder: short press → next mode
        if (enc.wasPressed()) {
            ui.nextMode();
            ui.onActivity();
        }
        // Encoder: long press (3s hold) → re-enter setup portal
        if (enc.wasLongPressed()) {
            Serial.println("Long-press — rebooting into setup portal...");
            store.clear();
            delay(200);
            ESP.restart();
        }
        // Encoder: rotation → timeframe
        int rot = enc.rotation();
        if (rot != 0) {
            ui.adjustTimeframe(rot);
            ui.updateData(claudeSnap, openaiSnap, store);
            ui.onActivity();
        }

        // Consume new data from API task
        if (dataReady) {
            if (xSemaphoreTake(dataMtx, 0) == pdTRUE) {
                dataReady = false;
                ui.updateData(claudeSnap, openaiSnap, store);
                xSemaphoreGive(dataMtx);
            }
        }

        // Consume new Claude.ai plan data (rate-limited, 5-minute cadence)
        if (planReady) {
            if (xSemaphoreTake(dataMtx, 0) == pdTRUE) {
                planReady = false;
                ui.updatePlan(planSnap);
                xSemaphoreGive(dataMtx);
            }
        }

        // Periodic updates
        ui.tick();
        ui.updateBacklight();

        // OTA
        if (WiFi.status() == WL_CONNECTED)
            ArduinoOTA.handle();

        break;
    }
    } // switch

    delay(5);
}
