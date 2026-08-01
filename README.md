# Split Flap Display Firmware

Firmware for a 12-module Chainlink/Denver3D-style split-flap display (ESP32),
including the self-hosted WiFi web UI (message, presets, automation, timer).

## What you need

- **PlatformIO** — install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code (free).
- **USB cable** for your ESP32 (the T-Display board used here).
- This folder, unzipped, opened in VS Code.

## Setup

1. Open this folder in VS Code (PlatformIO will auto-detect `platformio.ini`).
2. In `firmware/esp32/splitflap/`, copy `secrets.h.example` to a new file named
   `secrets.h`, and edit it:
   - `WIFI_SSID` / `WIFI_PASSWORD` — your WiFi network
   - `DEVICE_INSTANCE_NAME` — the mDNS name for the display (e.g. `splitflap`
     gives you `splitflap.local`)
   - Leave `MQTT_*` and `OTA_PASSWORD` as-is unless you plan to use MQTT or
     wireless (OTA) updates — see comments in the file for details.
3. `secrets.h` is required for the project to build — it's excluded from this
   package on purpose since it holds credentials.

## Build & upload

1. Connect the ESP32 via USB.
2. In VS Code's PlatformIO sidebar, select the **chainlink** environment
   (already the default — builds for 12 modules with the web UI enabled).
3. Click **Upload** (or from a terminal: `pio run -e chainlink --target upload`).
4. Open the Serial Monitor to confirm it boots. Once connected to WiFi, the
   display's screen shows its IP address — open that address (or
   `http://<DEVICE_INSTANCE_NAME>.local/`) in a browser for the web UI.

## More info

- `firmware/esp32/splitflap/WEB_UI_README.md` — details on the wireless web UI
  (message/presets/automation/timer), known limitations, and how it stores data.
- `firmware/esp32/README.md` — how the ESP32 build layers on top of the shared
  splitflap driver code.
- `platformio.ini` — all build environments. `chainlink` is what you want for
  a standard build; `chainlink_ota` is the same build but uploads wirelessly
  once you've flashed once over USB. The `advanced_*` environments are for
  different hardware (Chainlink base station, driver tester) — skip those
  unless you know you need them.

## License

Apache License 2.0 — see `LICENSE.txt`.
