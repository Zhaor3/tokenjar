# TOKENJAR

API usage tracker desktop gadget for Anthropic (Claude) and OpenAI APIs. ESP32-S3 + 2" IPS LCD + rotary encoder.

## Hardware

| Part | Spec |
|------|------|
| MCU | ESP32-S3 SuperMini (4 MB flash, no PSRAM) |
| Display | Waveshare 2" LCD, ST7789, 240×320 IPS, SPI |
| Input | EC11 rotary encoder with push button |
| Power | USB-C (from SuperMini board) |

### Wiring

```
ST7789 Display          ESP32-S3 SuperMini
──────────────          ──────────────────
VCC  ──────────────────  3.3V
GND  ──────────────────  GND
SCL  ──────────────────  GPIO 7   (SCK)
SDA  ──────────────────  GPIO 9   (MOSI)
CS   ──────────────────  GPIO 5
DC   ──────────────────  GPIO 4
RES  ──────────────────  GPIO 6
BL   ──────────────────  GPIO 10  (PWM backlight)

EC11 Encoder            ESP32-S3 SuperMini
────────────            ──────────────────
CLK  ──────────────────  GPIO 1
DT   ──────────────────  GPIO 2
SW   ──────────────────  GPIO 3   (active low)
+    ──────────────────  3.3V
GND  ──────────────────  GND
```

## Building & Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Clone
git clone <repo-url> tokenjar && cd tokenjar

# Build
pio run

# Flash
pio run -t upload

# Monitor serial
pio device monitor
```

## First-Boot Setup

1. Power the board via USB-C. The splash screen appears, then a **QR code**.
2. On your phone or laptop, connect to the WiFi network **tokenjar-setup**.
3. Open **http://192.168.4.1** (or scan the QR code).
4. Select your WiFi network from the scanned list, enter the password.
5. Paste your **Anthropic Admin API key** and/or **OpenAI Admin API key**.
6. Set daily and monthly budgets for each provider.
7. Click **Test Connections** to verify both keys work.
8. Click **Save & Reboot**. The device reboots, connects to WiFi, and starts fetching data.

### Getting Admin API Keys

- **Anthropic**: [console.anthropic.com/settings/admin-keys](https://console.anthropic.com/settings/admin-keys) — create an Admin key (starts with `sk-ant-admin`).
- **OpenAI**: [platform.openai.com/settings/organization/admin-keys](https://platform.openai.com/settings/organization/admin-keys) — create an Admin key.

Both require organization admin access.

## Usage

| Action | Effect |
|--------|--------|
| **Short press** encoder button | Cycle through modes: Claude Today → Claude Month → OpenAI Today → OpenAI Month → Combined → Settings |
| **Rotate** encoder | Change timeframe: 1h / 6h / 24h / 7d / 30d |
| **Long press** (>1 s) | Force immediate API refresh |

Modes for unconfigured providers are hidden automatically.

## Modes

1. **Claude Today** — today's spend, budget %, token count, 24h sparkline
2. **Claude Month** — month-to-date spend, projected total
3. **OpenAI Today** — same layout
4. **OpenAI Month** — same layout
5. **Combined** — totals across both providers
6. **Settings** — WiFi status, IP address, free heap, uptime

## OTA Updates

If you set an OTA password during setup, you can push firmware wirelessly:

```bash
pio run -t upload --upload-port tokenjar.local
```

The device advertises itself via mDNS as `tokenjar.local`.

## Fonts

The firmware ships with LVGL's built-in Montserrat at 10/12/14/48 px. For the intended design (Inter 72 px hero number, JetBrains Mono labels), generate custom fonts with [LVGL's font converter](https://lvgl.io/tools/fontconverter):

```
lv_font_conv --font Inter-Bold.ttf -r 0x20-0x7E --size 72 --format lvgl --bpp 4 -o src/font_inter_72.c
lv_font_conv --font JetBrainsMono-Regular.ttf -r 0x20-0x7E --size 12 --format lvgl --bpp 4 -o src/font_jbm_12.c
```

Then update `src/theme.h` to reference the new font symbols.

## Configuration

Edit `src/config.h` to change pin assignments, default budgets, refresh intervals, or backlight levels. All settings are `constexpr` or `#define` at the top of the file.

Runtime settings (WiFi, API keys, budgets, timezone) are stored in NVS and configured through the captive portal.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Display is white or garbled | Check SPI wiring. Try adding `-DTFT_INVERSION_ON` or `-DTFT_INVERSION_OFF` to `build_flags`. Try `tft.setRotation(2)` in `main.cpp`. |
| Colors are wrong (red/blue swapped) | Toggle `tft.setSwapBytes()` in `main.cpp` or add `-DTFT_RGB_ORDER=TFT_BGR` to `build_flags`. |
| "$0.00" on all screens | Verify your API key is an **Admin** key, not a regular API key. Check serial monitor for HTTP error codes. |
| WiFi won't connect | The device falls back to cached data after 30 s. Long-press to retry. To re-enter setup, erase NVS: `pio run -t erase`. |
| OTA upload fails | Ensure the OTA password matches what was set during setup. The device must be on the same network. |
| Build fails with "no space" | The 4 MB flash is tight with OTA. Disable unused fonts in `lv_conf.h` or remove OTA by using `default.csv` partitions. |

## License

MIT
