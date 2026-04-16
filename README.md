<div align="center">

# TOKENJAR

**A tiny desk gadget that tracks your AI API spending in real time.**

Claude Admin API &bull; OpenAI Admin API &bull; Claude.ai Pro/Max Plan Usage

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange?logo=platformio)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![GitHub release](https://img.shields.io/github/v/tag/Zhaor3/tokenjar?label=version)](https://github.com/Zhaor3/tokenjar/releases)

<!-- Replace with your own photo of the device -->
<!-- ![TOKENJAR on desk](docs/hero.jpg) -->

</div>

---

## Why

You're burning through API credits across Claude and OpenAI — Cursor, Copilot, custom agents, scripts — and the bills add up fast. TokenJar sits on your desk and shows exactly how much you've spent **right now**, without opening a browser tab.

## What It Tracks

| Source | Data | How |
|--------|------|-----|
| **Anthropic Admin API** | Spend today/month, tokens, 24h sparkline, budget % | `sk-ant-admin...` key |
| **OpenAI Admin API** | Spend today/month, tokens, 24h sparkline, budget % | `sk-admin-...` key |
| **Claude.ai Subscription** | 5h session %, weekly limit %, Opus/Sonnet split, extra usage $ | Session cookie |

> **Each provider is optional.** Configure just Claude, just OpenAI, just the session cookie, or all three. TokenJar adapts the screen ring automatically.

---

## Features

- **Real-time cost tracking** — refreshes every 60 seconds (Admin API) / 5 minutes (Claude.ai plan)
- **Any interaction refreshes data** — spin or press the encoder and data updates immediately
- **Multiple timeframes** — rotate the encoder to switch between 1h / 6h / Today / 7d / 30d views
- **Budget progress bars** — set daily and monthly limits, see how close you are at a glance
- **Claude.ai Pro/Max plan screen** — 5-hour session usage, weekly cap, Opus vs Sonnet breakdown, extra usage dollars
- **Session cookie auto-refresh** — the device captures renewed cookies from `Set-Cookie` headers automatically
- **Horizontal or vertical** — choose your orientation on first boot; the UI adapts
- **QR code setup portal** — connect to the device's WiFi, scan the QR, configure everything from your browser
- **OTA updates** — push new firmware over WiFi, no USB needed after first flash
- **Auto-dimming backlight** — dims after 60s idle, deep dims after 5 minutes

---

## Hardware

| Part | Spec | ~Cost |
|------|------|-------|
| MCU | ESP32-S3 SuperMini (4 MB flash, no PSRAM) | $4 |
| Display | 2" IPS LCD, ST7789, 240x320, SPI (8-pin) | $3 |
| Input | EC11 rotary encoder with push button (KY-040) | $1 |
| Power | USB-C (from the SuperMini board) | - |

**Total: ~$8**

### Wiring

<details>
<summary><b>ST7789 Display (8-pin)</b></summary>

| Display Pin | ESP32-S3 GPIO | Notes |
|-------------|---------------|-------|
| VCC | 3.3V | Do **not** use 5V |
| GND | GND | |
| DIN | GPIO 9 | SPI MOSI |
| CLK | GPIO 7 | SPI Clock |
| CS | GPIO 5 | Chip Select |
| DC | GPIO 4 | Data / Command |
| RST | GPIO 6 | Reset |
| BL | GPIO 10 | Backlight (PWM) |

</details>

<details>
<summary><b>Rotary Encoder (KY-040, 5-pin)</b></summary>

```
   +---------------+
   |  GND  +  SW   |   <-- 3-pin side
   |                |
   |     (knob)     |
   |                |
   |    DT    CLK   |   <-- 2-pin side
   +---------------+
```

| Encoder Pin | Side | ESP32-S3 GPIO | Notes |
|-------------|------|---------------|-------|
| GND | 3-pin | GND | |
| + (VCC) | 3-pin | 3.3V | **Never 5V** |
| SW | 3-pin | GPIO 8 | Push button, active low |
| DT | 2-pin | GPIO 2 | Rotation signal B |
| CLK | 2-pin | GPIO 1 | Rotation signal A |

> If rotation feels backwards, swap CLK and DT wires (GPIO 1 and GPIO 2).

</details>

<details>
<summary><b>Pins to avoid on ESP32-S3 SuperMini</b></summary>

| Pin | Why |
|-----|-----|
| EN / RST | Chip reset — encoder press would reboot the board |
| GPIO 0 | BOOT button — can force download mode |
| GPIO 3 | Strapping pin (JTAG select) |
| GPIO 19 / 20 | USB D- / D+ — will crash |
| GPIO 26-32 | Internal SPI flash |
| GPIO 45 / 46 | Strapping pins |

</details>

---

## Getting Started

### Prerequisites

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html) or [VS Code + PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- USB-C cable
- ESP32-S3 USB drivers (usually auto-installed)

### Build & Flash

```bash
git clone https://github.com/Zhaor3/tokenjar.git
cd tokenjar
pio run -t upload
```

### First-Boot Setup

1. Power the board via USB-C. A **splash screen** appears, then a **QR code**.
2. On your **computer** (recommended) or phone, connect to WiFi: **`tokenjar-setup`**
3. Open **http://192.168.4.1** in your browser.
4. Fill in the setup form:
   - **WiFi network** and password
   - **Anthropic Admin API key** (optional)
   - **OpenAI Admin API key** (optional)
   - **Claude.ai session cookie** (optional — for Pro/Max plan tracking)
   - Daily / monthly budgets
   - Timezone
5. Click **Save & Reboot**.

> **Tip:** Use a **computer** (not a phone) for setup. Some phones aggressively disconnect from the setup WiFi because it has no internet. On a computer, the connection stays stable. Fill in all fields first, then click Save & Reboot once.

### Getting API Keys

<details>
<summary><b>Anthropic Admin API Key</b></summary>

1. Go to [console.anthropic.com/settings/admin-keys](https://console.anthropic.com/settings/admin-keys)
2. Click **Create Admin Key**
3. Copy the key (starts with `sk-ant-admin01-...`)
4. Requires **organization admin** access

</details>

<details>
<summary><b>OpenAI Admin API Key</b></summary>

1. Go to [platform.openai.com/settings/organization/admin-keys](https://platform.openai.com/settings/organization/admin-keys)
2. Click **Create Admin Key**
3. Copy the key (starts with `sk-admin-...`)
4. Requires **organization admin** access

</details>

<details>
<summary><b>Claude.ai Session Cookie (for Pro/Max plan tracking)</b></summary>

This tracks your **subscription usage** (5-hour session cap, weekly limit, Opus/Sonnet breakdown) — data that the Admin API does not expose.

1. Open [claude.ai](https://claude.ai) and log in
2. Press **F12** to open DevTools
3. Go to **Application** tab > **Cookies** > **https://claude.ai**
4. Find the row named **`sessionKey`**
5. Double-click the **Value** and copy it (starts with `sk-ant-sid01-...`)

The cookie auto-renews via `Set-Cookie` headers, so you typically only need to paste it once. If it eventually expires (e.g. you log out of claude.ai), grab a fresh one.

</details>

---

## Usage

| Action | Effect |
|--------|--------|
| **Press** encoder | Next screen + refresh all data |
| **Rotate** encoder | Change timeframe (1h / 6h / Today / 7d / 30d) + refresh all data |
| **Long press** (>1s) | Factory reset — reboots into setup portal |

### Screens

| Screen | What it shows |
|--------|---------------|
| **Claude** | Admin API spend, tokens, sparkline, budget bar (timeframe via rotation) |
| **Claude Plan** | 5h session %, weekly limit %, Opus/Sonnet split, extra usage $ |
| **OpenAI** | Admin API spend, tokens, sparkline, budget bar (timeframe via rotation) |
| **Settings** | WiFi status, IP address, free heap, uptime |

Only screens for configured providers appear. If you only set up Claude, you'll only see Claude screens.

---

## Configuration

Edit `src/config.h` to adjust compile-time defaults:

```cpp
constexpr uint32_t API_REFRESH_MS       = 60 * 1000;     // Admin API refresh (60s)
constexpr uint32_t IDLE_DIM_MS          = 60 * 1000;     // dim after 60s idle
constexpr uint32_t IDLE_DEEP_DIM_MS     = 5 * 60 * 1000; // deep dim after 5min

#define DEFAULT_CLAUDE_DAILY_BUDGET   5.0f
#define DEFAULT_CLAUDE_MONTHLY_BUDGET 100.0f
#define DEFAULT_OPENAI_DAILY_BUDGET   5.0f
#define DEFAULT_OPENAI_MONTHLY_BUDGET 100.0f
```

Runtime settings (WiFi, API keys, budgets, timezone) are stored in NVS flash and configured through the captive portal.

---

## OTA Updates

Set an OTA password during setup, then push firmware wirelessly:

```bash
pio run -t upload --upload-port tokenjar.local
```

The device advertises via mDNS as `tokenjar.local`.

---

## How It Works

```
                    ESP32-S3
                   +--------+
                   |        |
  Encoder -------->| Core 1 |----> LVGL UI (240x320 LCD)
  (press/spin)     |  loop  |       - provider screens
                   |        |       - plan screen
                   +--------+       - settings screen
                   |        |
                   | Core 0 |----> API Task (FreeRTOS)
                   |  task   |       - Claude Admin API (60s)
                   +--------+       - OpenAI Admin API (60s)
                       |            - Claude.ai scraper (5min)
                       v
                   NVS Flash
                   (settings, cache)
```

**Admin APIs** — standard REST calls with admin keys. Fetches cost reports and usage data.

**Claude.ai Plan** — scrapes undocumented `claude.ai/api/organizations/{id}/usage` endpoints using a session cookie with Chrome-mimicking headers to pass Cloudflare. Extracts 5-hour session utilization, 7-day weekly limits, per-model breakdown, and overage billing.

**Data flow** — the API task on Core 0 fetches data and writes it to shared snapshots under a mutex. The main loop on Core 1 consumes the data and pushes it to LVGL screens. Any encoder interaction also wakes the API task for an immediate refresh.

---

## Troubleshooting

<details>
<summary><b>Common Issues</b></summary>

| Symptom | Fix |
|---------|-----|
| **Blank / white screen** | Check SPI wiring. Ensure `USE_FSPI_PORT` is in `build_flags` (critical for ESP32-S3). |
| **Colors wrong** | Toggle `-DTFT_INVERSION_ON` / `OFF` and check `-DTFT_RGB_ORDER=0` in `platformio.ini`. |
| **"$0.00" on all screens** | Your key must be an **Admin** key, not a regular API key. Also: $0 is correct if you haven't made API calls today. |
| **Portal page won't load** | Connect to `tokenjar-setup` WiFi, open `http://192.168.4.1` directly. Use a **computer** — phones may disconnect due to "no internet" detection. |
| **WiFi keeps disconnecting during setup** | Use a computer instead of a phone. Fill in all fields first, then Save & Reboot in one go. |
| **Claude Plan shows "AUTH EXPIRED"** | Your session cookie expired. Grab a fresh one from claude.ai DevTools and re-enter it via the portal. |
| **Claude Plan shows "CF BLOCKED"** | Cloudflare is challenging the request. Usually temporary — wait 5 minutes and it auto-retries. |
| **WiFi connect timeout** | Device falls back to cached data after 30s. Long-press to re-enter setup. |
| **OTA upload fails** | Check OTA password matches. Device must be on the same local network. |
| **Build fails "no space"** | Current build uses ~32% of 4 MB flash. Disable unused fonts in `platformio.ini` if needed. |
| **Encoder press reboots board** | SW pin is connected to EN/RST. Rewire to GPIO 8. |

</details>

<details>
<summary><b>ESP32-S3 SPI Crash (TFT_eSPI)</b></summary>

TFT_eSPI has a known issue on ESP32-S3: `FSPI` equals `0`, so `REG_SPI_BASE(0)` returns null, causing a `StoreProhibited` crash in `tft.init()`. The fix is `-DUSE_FSPI_PORT` in build flags, which forces `SPI_PORT = 2`. Always do a full clean build (`pio run -t clean`) after adding this flag.

</details>

<details>
<summary><b>API Endpoints Used</b></summary>

**Admin APIs (official, documented):**
```
Anthropic:
  GET /v1/organizations/cost_report?starting_at=...&ending_at=...
  GET /v1/organizations/usage_report/messages?starting_at=...&ending_at=...

OpenAI:
  GET /v1/organization/costs?start_time=<unix>&limit=31
  GET /v1/organization/usage/completions?start_time=<unix>&limit=31
```

**Claude.ai Plan (undocumented, reverse-engineered):**
```
GET https://claude.ai/api/organizations
GET https://claude.ai/api/organizations/{uuid}/usage
GET https://claude.ai/api/organizations/{uuid}/overage_spend_limit
```

</details>

---

## Project Structure

```
tokenjar/
  src/
    main.cpp                  # Setup, loop, captive portal, state machine
    config.h                  # Pin assignments, timing, display constants
    theme.h                   # Colors, fonts, animation timing
    api/
      claude_client.cpp/h     # Anthropic Admin API client
      openai_client.cpp/h     # OpenAI Admin API client
      claude_web_client.cpp/h # Claude.ai session scraper
      claude_plan.h           # Plan snapshot data struct
      usage_provider.h        # Usage snapshot + timeframe types
    storage/
      settings_store.cpp/h    # NVS persistence (WiFi, keys, cache)
    input/
      encoder.cpp/h           # Rotary encoder driver (ESP32Encoder)
    ui/
      ui_manager.cpp/h        # Screen lifecycle, mode ring, backlight
      screen_provider.cpp/h   # Vertical admin API screen
      screen_provider_h.cpp/h # Horizontal admin API screen
      screen_plan.cpp/h       # Vertical plan screen
      screen_plan_h.cpp/h     # Horizontal plan screen
      screen_settings.cpp/h   # Vertical settings screen
      screen_settings_h.cpp/h # Horizontal settings screen
      i_screen_provider.h     # Provider screen interface
      i_screen_plan.h         # Plan screen interface
      i_screen_settings.h     # Settings screen interface
  platformio.ini              # Build configuration
  partitions.csv              # Flash partition table
```

---

## Tech Stack

| Component | Library / Version |
|-----------|-------------------|
| Framework | Arduino (ESP-IDF under the hood) |
| Platform | espressif32 @ 6.9.0 |
| Graphics | LVGL 9.2.2 |
| Display driver | TFT_eSPI 2.5.43 |
| JSON | ArduinoJson 7.2.1 |
| Encoder | ESP32Encoder 0.11.6 |
| QR Code | QRCode 0.0.1 |

---

## Contributing

Pull requests welcome. If you add support for a new API provider or build a case/enclosure, please share!

1. Fork the repo
2. Create your feature branch (`git checkout -b feature/my-change`)
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---

<div align="center">

**Built with an ESP32-S3 and mass amounts of coffee.**

If TokenJar saved you from a surprise API bill, consider giving it a star.

</div>
