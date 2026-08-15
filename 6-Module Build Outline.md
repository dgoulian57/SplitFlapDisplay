# 6-Module Split-Flap Display — Build & Video Outline

A single Chainlink Driver + Chainlink Buddy [T-Display] supports up to 6 character modules, so this build is a clean, self-contained "starter kit" scope — good for a video since every part maps to exactly this build with no leftover/spare components.

Fill in the **Script Notes** line under each phase as you film. Checklist items are pulled from your existing docs (`docs/v2/OrderingEasy.md`, `docs/ElectronicsGuide.md`, `docs/v2/Assembly.md`, `docs/Flaps.md`) and your prior 12-module Denver3D build.

---

## 1. Ordering Parts

**Electronics** (Bezek Labs / Etsy)
- [ ] Chainlink Driver v1.1 — 1x, add 6x 28BYJ-48 motors at checkout
- [ ] Sensor kit v2 (6-pack, with magnets + headers, cables included)
- [ ] Chainlink Buddy [T-Display] — 1x, include ESP32 T-Display module

**Flaps**
- [ ] Decide: pre-printed "Epilogue" 52-flap sets (6-pack) vs. blank flaps + vinyl letter stickers
  - If DIY: 1x sticker pack per 2 modules (so 3 packs for 6 modules)

**Hardware** (McMaster-Carr or equivalent)
- [ ] M4 x 10mm button-head bolts — need 60 (10/module); order with margin
- [ ] M4 hex nuts — need 60 (10/module); order with margin

**Power & wiring**
- [ ] 22AWG wire for 3.3V power
- [ ] 12V power supply, ≥1.5A rated for 6 modules + margin (assume online listings overstate actual current)
- [ ] 20AWG wire for 12V power/ground

**Mechanical frame**
- [ ] Since you're printing rather than laser-cutting: confirm/download the Denver3D remix files (Printables) sized for 6 modules — no separate acrylic order needed
- [ ] Note filament type/color for the frame (PLA vs PETG) for the parts list on screen

> **Lessons learned from prior builds:**
> - Colored flaps (g/r/y/p/w) print noticeably thicker than plain letter flaps and caused motor stalls on the 12-module build — worth calling out on camera if you order the pre-printed "Epilogue" set (which includes color blocks), or consider skipping them
> - With exactly 6 modules, `NUM_MODULES` divides evenly (Chainlink requires a multiple of 6) — no "phantom slot" `SENSOR_ERROR` warnings to explain like the 12-module build had with only 10 physical modules installed. Worth a line in the script since it trips people up.

**Script notes:** _______________________________________________

---

## 2. Printing the 3D Parts

- [ ] Slice and print Denver3D frame parts for 6 modules on the Bambu P1S (struts, wheels/spacers, enclosure panels, motor mounts — whatever the remix defines per module)
- [ ] Print 6x flap drum sets (struts + 2 wheels each)
- [ ] Print any optional jigs if going the DIY flap route: scoring jig (`3d/tools/scoring_jig.scad`), punch jig (`3d/tools/punch_jig.scad`), flap container (`3d/tools/flap_container.scad`)
- [ ] Post-process: remove supports, test-fit a bolt/nut in a spacer piece before committing to a full print run
- [ ] Inventory check: lay out printed parts for all 6 modules before moving to assembly (easy to catch a missing/failed piece here instead of mid-assembly)

> **Lessons learned from prior builds:**
> - No filament/print-setting failures have been logged from prior builds yet — this is genuinely new ground to document on camera.
> - Do a dry-fit of a bolt/nut in one printed spacer piece before committing to a full print run of all 6 modules' drum parts (cheap way to catch a sizing issue early).

**Script notes:** _______________________________________________

---

## 3. Assembly

**Electronics**
- [ ] Assemble the Chainlink Driver PCB
- [ ] Assemble the Chainlink Buddy [T-Display]
- [ ] Connect Chainlink Driver to Chainlink Buddy (ribbon cable)
- [ ] Assemble 6x sensor PCBs

**Mechanical, per module (x6)**
- [ ] Assemble flap drum (bolt/nut on spacer, 4 struts, 2 wheels, CA glue + accelerator — don't twist the drum before gluing)
- [ ] Mount motor + sensor PCB into left enclosure piece
- [ ] Insert magnet
- [ ] Attach top/bottom/right enclosure pieces
- [ ] Install flaps onto the drum

**Bring-up**
- [ ] Connect all 6 modules + power to the Chainlink Buddy
- [ ] Flash firmware (PlatformIO `chainlink` environment)
- [ ] Power on and confirm each module homes correctly

> **Lessons learned from prior builds:**
> - Hall sensors used here are unipolar — they only respond to one magnetic pole. A reversed magnet never triggers the home sensor at all (bit two modules on the 12-module build this way). Before final assembly, hand-rotate each drum with `sensor_check.py` running and confirm the sensor toggles cleanly — cheap to catch now, painful after gluing.
> - A too-long screw or even a loose screw fragment lodged in a drum can freeze a module completely (looks identical to a wiring/comms fault from software) — even on a module that was previously working fine. If a module suddenly won't move at all, physically check for debris/screw length before assuming it's electrical.
> - Don't twist the flap drum wheels relative to each other while the CA glue cures — a twisted drum makes flaps hang at an angle and bind against the side windows later.
> - Route hall-sensor signal wires away from motor power wires. Running them parallel/bundled induced electrical noise that caused intermittent, non-repeating mis-landings on one module in the 12-module build — easy to prevent at wiring time, tedious to diagnose after the fact.

**Script notes:** _______________________________________________

---

## 4. Test & Calibration

- [ ] `sensor_check.py` — hand-rotate each drum, confirm home sensor toggles cleanly on all 6 modules before applying power
- [ ] Power on, confirm all 6 modules home without stalling
- [ ] `splitflap_calibrate.py` (or `calibrate_single.py` per-module) — set flap-blank alignment offsets, save to flash
- [ ] `validate_chars.py` — step through every character on each module, confirm correct flap displays
- [ ] Spot-check for mechanical issues: drum twist, flap binding on side windows, loose enclosure bolts
- [ ] Run a short animation/message across all 6 modules as a final "it works" shot for the video

> **Lessons learned from prior builds:**
> - **Always power-cycle the display before running `calibrate_single.py`** — even between two consecutive attempts on the *same* module. Re-running without power-cycling can silently fail every command (firmware reports success, drum never moves) due to stale server-side state that only clears on a real reboot.
> - **Never let an automated/diagnostic script send `save_all_offsets`.** A buggy diagnostic script once triggered rapid resets mid-motion and saved zeroed-out offsets for every module on the 12-module build, wiping all calibration. Only a deliberate, human-confirmed calibration step should ever save.
> - **Firmware telemetry can self-report success even when the physical drum didn't move** — there's no continuous position feedback, only the home sensor once per revolution. Always visually confirm the flap actually moved; don't trust the on-screen state alone.
> - **Avoid Scott's original web UI calibration dialog** (right-click a module → HALF/TENTH nudge) and the `INCREASE_OFFSET` HALF/TENTH commands generally — both accumulate rounding error and were abandoned in favor of `calibrate_single.py`'s `set_positions` approach, which uses the firmware's own accurate step math.
> - **Diagnose by pattern:** if a module fails at the *exact same physical position* every time, that's a mechanical snag (disassemble and inspect the drum/spool). If it fails with *varying* magnitude/position, that's more consistent with speed tuning or electrical noise — retry with a fresh power cycle first rather than jumping to disassembly.
> - If a module consistently lands a fixed amount short (e.g. exactly 1 flap) on *every* move regardless of distance, that points to marginal motor deceleration/torque rather than a snag — a one-time compensating offset nudge can fix it without disassembly.
> - The Chrome-based web UI (Web Serial) holds the serial port — close/disconnect it before running any Python calibration script, or the script won't be able to open the port.
> - Set `PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python` as an environment variable before running the calibration Python scripts (protobuf version incompatibility otherwise).
> - If you see frequent silent stalls (firmware says a move succeeded but the drum didn't turn), it's usually a motor speed/timing issue, not a logic bug. For 12V 28BYJ-48 motors, `MIN_PERIOD_MICROS = 4700` with `ACCEL_TIME_MICROS = 300000` (in `firmware/src/generate_acceleration.py`) was the value that worked out after testing — the original default of 2400 stalled frequently. Note the relationship isn't monotonic (5200 was worse than 3600), so don't just guess in one direction.

**Script notes:** _______________________________________________

---

## Optional stretch goal: wireless web UI

If you plan to add the WiFi web UI (message/presets/automation/timer/calibration wizard) built for the 12-module display, a few things worth carrying over:

> **Lessons learned from prior builds:**
> - This project's custom multi-folder `build_src_filter` breaks PlatformIO's automatic Library Dependency Finder — every library actually used (`WiFi`, `WebServer`, `ESPmDNS`, `Preferences`, etc.) must be explicitly listed in `lib_deps`, even ones bundled with the ESP32 Arduino core.
> - Store web UI settings (presets/automation) under a separate `Preferences` (NVS) namespace from the module-offset config (`/config.pb`) — keeps a UI bug from ever risking a repeat of the offset-wipe incident above.
> - Run `pio run -e chainlink -t upload` from the repo root (where `platformio.ini` lives), not from inside `firmware/esp32/splitflap/`.
> - If building the local JS web UI from source: it's an npm workspaces monorepo — run `npm install` then `npm run build` from the root `js/` directory (not a package subfolder). The interactive dev server (`npm run example-webserial-basic`) hung indefinitely on Rick's Mac; the reliable path was building once and serving the static output with `npx serve -s packages/example-webserial-basic/build -l 3000`.
> - If you build an in-browser calibration wizard, mirror the same safety pattern as `calibrate_single.py`: human visual confirmation before `SET_OFFSET`/save, poll for actual move completion rather than a fixed sleep, and skip issuing a move if the module's already at blank.

---

## Reference docs already in this project
- `docs/v2/OrderingEasy.md` — full ordering guide
- `docs/ElectronicsGuide.md` — Chainlink Driver/Buddy assembly
- `docs/v2/Assembly.md` — mechanical + firmware assembly steps
- `docs/Flaps.md` — DIY flap cutting/stickering + jig printing
- `sensor_check.py`, `calibrate_single.py`, `splitflap_calibrate.py`, `validate_chars.py` — your working calibration/test scripts from the 12-module build
