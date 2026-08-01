# Wireless web UI

Adds WiFi control to the Chainlink firmware: the ESP32 joins your home network
and serves its own control page — no USB, no laptop/server needing to stay on.

## Files added

- `web_ui_task.h` / `web_ui_task.cpp` — new FreeRTOS task: connects to WiFi,
  runs an HTTP server + JSON API, handles automation (clock/rotate/schedule).
- `web_ui_page.h` — the actual web page (HTML/CSS/JS), embedded as a single
  PROGMEM string. No build step, no separate npm project.
- `main.cpp` / `platformio.ini` — wired in behind a `WEB_UI` build flag,
  following the same pattern as the existing `MQTT`/`HTTP` flags.

## Enabling it

Already flipped on for you: `env:chainlink` in `platformio.ini` now has
`-DWEB_UI=true`. No new entries needed in `secrets.h` — it reuses
`WIFI_SSID`, `WIFI_PASSWORD`, and `DEVICE_INSTANCE_NAME`, which should already
be set from the OTA/MQTT setup you did previously.

Build and flash as usual:
```
pio run -e chainlink --target upload --upload-port /dev/cu.wchusbserial5B340452251
```

## Using it

On boot, the T-Display's top line shows the WiFi IP once connected, and the
second line shows the web UI address. Open that in any browser on the same
network — either `http://<DEVICE_INSTANCE_NAME>.local/` (mDNS) or the raw IP
if mDNS doesn't resolve on your network.

The page has three sections:
- **Message** — type text, hit Send. Shows immediately, same as typing into
  the USB web UI. Character set/case rules are unchanged (lowercase g/p/r/w/y
  still select color blocks).
- **Presets** — save named messages, show any of them with one tap, delete
  ones you don't need.
- **Automation** — pick one mode: Off, Clock (shows HH:MM, updates every
  minute), Rotate (cycles all presets on a timer you set), or Schedule (pick
  specific times of day, each tied to a preset).

Everything (presets + automation config) persists in flash (NVS/Preferences),
separate from the existing module-offset calibration storage — so it survives
reboots but won't interfere with `/config.pb` or the calibration scripts.

## Known limitations / things to watch for

- Time-based features (clock/schedule) need NTP to succeed once after boot.
  If WiFi is up but there's no internet route, those modes will silently do
  nothing until sync succeeds — the serial log will say so.
- Timezone is hardcoded to `MST7` (Arizona, no DST). If that ever changes,
  update `WEB_UI_TIMEZONE` in `web_ui_task.cpp`.
- This is a synchronous web server (`WebServer.h`, not async) — fine for one
  browser tab at a time, which matches how you'd actually use this.
- Not tested against your actual hardware yet (I don't have USB/serial access
  from this environment) — first flash should be treated like any other
  firmware change: watch the serial monitor for the WiFi/NTP log lines before
  assuming it's working.

## About the copy error

Copying the whole repo into the project folder pulled in
`software/chainlink/js/node_modules` — 423MB and ~97 symlinks. That's almost
certainly what Finder choked on (large symlink-heavy `node_modules` trees are
a classic source of macOS copy errors). It's not needed for firmware work and
regenerates via `npm install` if you ever need the local web-serial UI again —
safe to delete from the copy if you want to tidy up.
