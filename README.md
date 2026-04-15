# TOKENJAR

A minimal desk gadget that shows real-time API spending and token usage for **Anthropic Claude** and **OpenAI** — built on an ESP32-S3 SuperMini with a 2" IPS LCD.

![TOKENJAR showing Claude spend with orange accent](docs/preview.jpg)

## What it tracks

TokenJar connects to the **Admin API** of each platform and displays:

| Metric | Claude | OpenAI |
|--------|--------|--------|
| Today's spend ($) | ✅ | ✅ |
| Month-to-date spend ($) | ✅ | ✅ |
| Token count (today / month) | ✅ | ✅ |
| 24-hour sparkline | ✅ | ✅ |
| Budget % bar | ✅ | ✅ |

> **Note:** The Admin API tracks **API platform usage** — calls made via SDK, Cursor, code, etc. using `sk-ant-api...` keys.  
> It does **not** track Claude.ai Pro web conversations, session limits, or weekly limits — those are internal to Claude.ai and have no public API.

---

## Hardware

| Part | Spec |
|------|------|
| MCU | ESP32-S3 SuperMini (4 MB flash, no PSRAM) |
| Display | Waveshare 2" LCD, ST7789, 240×320 IPS, SPI |
| Input | EC11 rotary encoder with push button |
| Power | USB-C (from SuperMini board) |

### Wiring

#### ST7789 Display (8-pin)

| Display Pin | Connects to | Notes |
|-------------|-------------|-------|
| **VCC** | 3.3V | Do **not** use 5V |
| **GND** | GND | |
| **DIN** | GPIO 9 | Data In (SPI MOSI) |
| **CLK** | GPIO 7 | SPI clock (SCK) |
| **CS**  | GPIO 5 | Chip select |
| **DC**  | GPIO 4 | Data / command select |
| **RST** | GPIO 6 | Reset |
| **BL**  | GPIO 10 | Backlight (PWM dimming) |

#### Rotary Encoder (KY-040, 5-pin)

The encoder has **3 pins on one side** and **2 pins on the other**:

```
   ┌─────────────┐
   │  GND +  SW  │   ← 3-pin side
   │             │
   │   (knob)    │
   │             │
   │    DT  CLK  │   ← 2-pin side
   └─────────────┘
```

| Encoder Pin | Side | Connects to | Notes |
|-------------|------|-------------|-------|
| **GND** | 3-pin | GND | |
| **+** (VCC) | 3-pin | **3.3V only** — never 5V |
| **SW** | 3-pin | GPIO 8 | Push-button, active low |
| **DT** | 2-pin | GPIO 2 | Rotation signal B |
| **CLK** | 2-pin | GPIO 1 | Rotation signal A |

> **If rotation direction feels backwards**, swap the **CLK** and **DT** wires (GPIO 1 ↔ GPIO 2).  
> **No external pull-up resistors needed** — the ESP32-S3 uses its internal pull-up on the SW pin.

#### ⚠️ Pins to AVOID on ESP32-S3 SuperMini

If the device **reboots when you press or spin the encoder**, you probably hit one of these:

| Pin | Why to avoid |
|-----|--------------|
| **EN** / **RST** | Chip reset — connecting SW here literally reboots the board on every press |
| **GPIO 0** | BOOT button — pulling low can force download mode |
| **GPIO 3** | Strapping pin (JTAG select) — can interfere at boot |
| **GPIO 19 / 20** | USB D- / D+ — used by USB CDC, will crash |
| **GPIO 26–32** | Internal SPI flash — any use will hang the chip |
| **GPIO 45 / 46** | Strapping pins (flash voltage / boot mode) |

**Safe GPIO inputs for the encoder:** 1, 2, 4, 8, 11, 12, 13, 14, 15, 16, 17, 18, 21 (most are ADC-capable). The display already uses 4, 5, 6, 7, 9, 10, so avoid those.

---

## Building & Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Clone
git clone https://github.com/Zhaor3/tokenjar && cd tokenjar

# Build
pio run

# Flash
pio run -t upload

# Serial monitor
pio device monitor
```

---

## First-Boot Setup

1. Power the board. The splash screen appears, then a **QR code**.
2. On your phone or laptop, connect to WiFi network **`tokenjar-setup`**.
3. Open **http://192.168.4.1** in your browser.
4. Type your WiFi network name (or click a scanned network) and enter the password.
5. Paste your **Anthropic Admin API key** and/or **OpenAI Admin API key**.
6. Set daily and monthly budgets for each provider.
7. Click **Test Connections** to verify the keys work.
8. Click **Save & Reboot**. The device connects to WiFi and starts fetching data.

> **To re-enter setup at any time:** Long-press the encoder button for 1 second while the main UI is running. This clears all settings and reboots into the setup portal.

### Getting Admin API Keys

- **Anthropic**: [console.anthropic.com/settings/admin-keys](https://console.anthropic.com/settings/admin-keys)  
  Create an **Admin key** — starts with `sk-ant-admin01-...`  
  Requires organization admin access.

- **OpenAI**: [platform.openai.com/settings/organization/admin-keys](https://platform.openai.com/settings/organization/admin-keys)  
  Create an **Admin key** — starts with `sk-admin-...`  
  Requires organization admin access.

---

## Using the Device

| Action | Effect |
|--------|--------|
| **Short press** encoder | Cycle through screens |
| **Rotate** encoder | Change timeframe: 1h / 6h / 24h / 7d / 30d |
| **Long press** (>1 s) during run | Wipe settings, reboot to setup portal |
| **Long press** during splash | Same — force setup portal |

### Screens

1. **Claude Today** — today's spend, budget %, token count, sparkline
2. **Claude Month** — month-to-date spend and tokens
3. **OpenAI Today** — same layout
4. **OpenAI Month** — same layout
5. **Combined** — totals across both providers
6. **Settings** — WiFi status, IP, free heap, uptime

Screens for unconfigured providers are automatically hidden.

---

## Configuration

Edit `src/config.h` to adjust:

```cpp
constexpr uint32_t API_REFRESH_MS   = 60 * 1000;   // refresh interval (60s)
constexpr uint32_t IDLE_DIM_MS      = 60 * 1000;   // dim after 60s idle
constexpr uint32_t IDLE_DEEP_DIM_MS = 5 * 60 * 1000; // deep dim after 5min

#define DEFAULT_CLAUDE_DAILY_BUDGET   5.0f
#define DEFAULT_CLAUDE_MONTHLY_BUDGET 100.0f
#define DEFAULT_OPENAI_DAILY_BUDGET   5.0f
#define DEFAULT_OPENAI_MONTHLY_BUDGET 100.0f
```

Runtime settings (WiFi credentials, API keys, budgets, timezone) are stored in NVS and configured through the captive portal — never hardcoded.

---

## OTA Updates

Set an OTA password during first-boot setup, then push firmware wirelessly:

```bash
pio run -t upload --upload-port tokenjar.local
```

The device advertises itself via mDNS as `tokenjar.local`.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Blank / white screen | Check SPI wiring. Verify `USE_FSPI_PORT` is in `build_flags` (critical for ESP32-S3 — fixes a null-pointer crash in TFT_eSPI). |
| Colors wrong (red/blue swapped) | Ensure `-DTFT_RGB_ORDER=0` is in `build_flags`. Toggle `-DTFT_INVERSION_ON` / `OFF` if polarity is inverted. |
| "$0.00" on all screens | Your API key must be an **Admin** key (not a regular API key). Check serial monitor for `[CLAUDE] HTTP 4xx` errors. Also: $0 is correct if you haven't made any direct API calls today. |
| "Valid=1 but data is empty" | The Admin API only reports API platform usage. Claude.ai Pro web conversations don't appear here. |
| Portal page won't open | Make sure you're connected to `tokenjar-setup` WiFi. Try `http://192.168.4.1` directly (not HTTPS). Some Android devices auto-switch back to mobile data. |
| WiFi connect timeout | Device falls back to cached data after 30 s. Long-press to enter setup portal and reconfigure. |
| OTA upload fails | Ensure OTA password matches setup. Device must be on the same network. |
| Build fails "no space" | Disable unused fonts in `include/lv_conf.h`. The current build uses ~31% of 4 MB flash. |

---

## Technical Notes

### ESP32-S3 SPI Bug

TFT_eSPI has a critical bug on ESP32-S3: the `FSPI` constant equals `0`, but `REG_SPI_BASE(0)` returns `0x00`, causing a null-pointer crash (`StoreProhibited` at `0x00000010`) inside `tft.init()`.

Fix: `-DUSE_FSPI_PORT` in `build_flags` forces `SPI_PORT = 2`, avoiding the crash. This must be combined with a **full clean rebuild** (`pio run -t clean`) after adding the flag.

### Admin API Endpoints Used

```
Anthropic:
  GET /v1/organizations/cost_report?starting_at=...&ending_at=...
  GET /v1/organizations/usage_report/messages?starting_at=...&ending_at=...

OpenAI:
  GET /v1/organization/costs?start_time=<unix>&limit=31
  GET /v1/organization/usage/completions?start_time=<unix>&limit=31
```

### Partition Layout

Single-app, no OTA partitions (for maximum flash space with 4 MB boards):

```
nvs       0x9000    20 KB
otadata   0xe000     8 KB
app0      0x10000  ~3.9 MB
```

---

## License

MIT
