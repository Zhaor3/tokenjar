#include <Arduino.h>
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
#include "api/claude_client.h"
#include "api/openai_client.h"
#include "input/encoder.h"
#include "ui/ui_manager.h"

// ── Globals ──────────────────────────────────────────────────────

static TFT_eSPI        tft;
static SettingsStore    store;
static Encoder          enc;
static ClaudeUsageClient claudeApi;
static OpenAIUsageClient openaiApi;
static UIManager        ui;

// Shared data between main loop and API task
static SemaphoreHandle_t dataMtx;
static UsageSnapshot     claudeSnap, openaiSnap;
static TaskHandle_t      apiTask = nullptr;
static volatile bool     dataReady = false;

// Portal mode
static WebServer*  portal_server = nullptr;
static DNSServer*  portal_dns    = nullptr;

// Display buffer
static uint8_t lvBuf[SCREEN_W * 20 * sizeof(lv_color_t)];

// ── App state machine ────────────────────────────────────────────

enum class State { SPLASH, PORTAL, CONNECTING, RUNNING };
static State state = State::SPLASH;
static uint32_t stateStart = 0;

// ── LVGL display flush ──────────────────────────────────────────

static void dispFlush(lv_display_t*, const lv_area_t* area, uint8_t* px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)px, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(lv_display_get_default());
}

static uint32_t lvTick() { return millis(); }

// ── API refresh task (core 0) ───────────────────────────────────

static void apiRefreshLoop(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(API_REFRESH_MS));

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
<div id="networks">Scanning...</div>
<label>WIFI PASSWORD</label>
<input type="password" id="wpass">
<hr>
<label>ANTHROPIC ADMIN API KEY <a href="https://console.anthropic.com/settings/admin-keys" target="_blank">(how to get this)</a></label>
<input type="text" id="ckey" placeholder="sk-ant-admin...">
<div class="row">
  <div><label>DAILY BUDGET ($)</label><input type="number" id="cbd" value="5" step="0.5"></div>
  <div><label>MONTHLY BUDGET ($)</label><input type="number" id="cbm" value="100" step="1"></div>
</div>
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
let selSSID='';
fetch('/scan').then(r=>r.json()).then(d=>{
  let el=document.getElementById('networks');el.innerHTML='';
  d.forEach(n=>{let div=document.createElement('div');
    div.textContent=n.ssid+' ('+n.rssi+'dBm)';
    div.onclick=()=>{document.querySelectorAll('#networks div').forEach(x=>x.classList.remove('sel'));
      div.classList.add('sel');selSSID=n.ssid;};
    el.appendChild(div);});
});
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
    body:JSON.stringify({ssid:selSSID,pass:document.getElementById('wpass').value,
      ckey:document.getElementById('ckey').value,
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
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    delay(100);

    portal_dns = new DNSServer();
    portal_dns->start(53, "*", WiFi.softAPIP());

    portal_server = new WebServer(80);

    portal_server->on("/", HTTP_GET, []() {
        portal_server->send_P(200, "text/html", PORTAL_HTML);
    });

    portal_server->on("/scan", HTTP_GET, []() {
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i = 0; i < n; i++) {
            if (i) json += ",";
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        json += "]";
        portal_server->send(200, "application/json", json);
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

    // Backlight PWM
    ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_RES);
    ledcAttachPin(PIN_TFT_BL, BL_PWM_CH);
    ledcWrite(BL_PWM_CH, BL_FULL);

    // TFT
    tft.init();
    tft.setRotation(0);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    // LVGL
    lv_init();
    lv_tick_set_cb(lvTick);
    lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, dispFlush);
    lv_display_set_buffers(disp, lvBuf, nullptr, sizeof(lvBuf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // NVS
    store.begin();

    // Encoder
    enc.begin();

    // Splash
    lv_obj_t* splash = UIManager::makeSplash();
    lv_screen_load(splash);
    state = State::SPLASH;
    stateStart = millis();
}

// ── Loop ─────────────────────────────────────────────────────────

void loop() {
    lv_timer_handler();
    enc.update();

    switch (state) {

    // ── SPLASH ───────────────────────────────────────────────────
    case State::SPLASH:
        if (millis() - stateStart >= SPLASH_MS) {
            if (!store.hasWiFi()) {
                portalSetup();
                lv_obj_t* qr = UIManager::makeQRSetup();
                lv_screen_load(qr);
                state = State::PORTAL;
            } else {
                WiFi.mode(WIFI_STA);
                WiFi.begin(store.ssid().c_str(), store.pass().c_str());
                lv_obj_t* conn = UIManager::makeConnecting(store.ssid().c_str());
                lv_screen_load(conn);
                state = State::CONNECTING;
                stateStart = millis();
            }
        }
        break;

    // ── CAPTIVE PORTAL ───────────────────────────────────────────
    case State::PORTAL:
        portal_dns->processNextRequest();
        portal_server->handleClient();
        break;

    // ── CONNECTING ───────────────────────────────────────────────
    case State::CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
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

            // Init UI (builds mode ring from configured keys)
            ui.init(store);
            ui.updateData(claudeSnap, openaiSnap, store);

            // Start API refresh task on core 0
            dataMtx = xSemaphoreCreateMutex();
            xTaskCreatePinnedToCore(apiRefreshLoop, "api", 8192,
                                    nullptr, 1, &apiTask, 0);
            xTaskNotifyGive(apiTask);   // trigger first fetch

            state = State::RUNNING;
        } else if (millis() - stateStart > WIFI_CONNECT_TIMEOUT) {
            // Timeout — proceed with cached data
            store.loadCache("claude", claudeSnap);
            store.loadCache("openai", openaiSnap);
            ui.init(store);
            ui.updateData(claudeSnap, openaiSnap, store);
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
        // Encoder: long press → force refresh
        if (enc.wasLongPressed()) {
            if (apiTask) xTaskNotifyGive(apiTask);
            ui.onActivity();
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
