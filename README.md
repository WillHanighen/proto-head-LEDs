# Proto head LEDs

ESP32 firmware for two mirrored **8×24** WS2812B LED matrices, controlled over **WebSocket** (port 81). A browser UI (`led_controller.html`) draws on the grid, uploads images, runs animations, and adjusts brightness.

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32 (`esp32dev` in PlatformIO) |
| LEDs | WS2812B, **GRB** order (FastLED) |
| Left strip | GPIO **5**, 192 LEDs (8×24) |
| Right strip | GPIO **18**, 192 LEDs, **horizontally mirrored** vs left |
| Layout | Serpentine wiring per panel (`XY` / `XY_MIRROR` in firmware) |

Change `WIDTH`, `HEIGHT`, `LEFT_PIN`, `RIGHT_PIN`, or LED type in `src/main.cpp` if your build differs.

## Software

- [PlatformIO](https://platformio.org/) — `espressif32`, Arduino framework
- Libraries: **FastLED**, **WebSockets** (server), **ArduinoJson**

## Build and flash

1. Install [PlatformIO](https://platformio.org/install) (CLI or IDE extension).

2. Set WiFi in `src/main.cpp`:

   ```cpp
   const char* WIFI_SSID = "YOUR_SSID";
   const char* WIFI_PASS = "YOUR_PASSWORD";
   ```

3. From the project directory:

   ```bash
   pio run -t upload
   ```

4. Open the serial monitor (115200 baud) to see the ESP32’s IP after it connects.

## Web UI

Open `led_controller.html` in a browser (local file is fine). Set the WebSocket URL to:

`ws://<ESP32_IP>:81`

Then connect. The UI supports drawing, bucket fill, image upload (scaled to 8×24), solid fill, animations, brightness, and optional live push while drawing.

## WebSocket protocol

**Port:** `81`

### JSON (text frames)

Commands use a `cmd` field:

| `cmd` | Fields | Effect |
|-------|--------|--------|
| `mode` | `mode`: `"off"` \| `"animation"` \| `"static"` | Off, play animations, or hold static / framebuffer |
| `anim` | `type`: `0`–`3` | `0` pixel sweep, `1` row sweep, `2` column sweep, `3` rainbow |
| `brightness` | `value`: 1–255 | Global FastLED brightness |
| `fill` | `r`, `g`, `b` | Fill both panels with one color |

### Binary frames

Send **exactly** `WIDTH * HEIGHT * 3` bytes (576 for 8×24): row-major RGB for the logical matrix. The firmware maps pixels to both strips (right side mirrored).

## Serial

`Serial` is 115200 baud; on boot you should see WiFi progress and the assigned IP.
