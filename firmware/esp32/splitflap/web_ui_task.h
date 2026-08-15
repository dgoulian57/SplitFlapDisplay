/*
   Wireless (WiFi) control + web UI for the split-flap display.

   Joins the WiFi network configured in secrets.h (WIFI_SSID / WIFI_PASSWORD)
   and serves a self-contained web UI directly from the ESP32 (no separate
   server needed). Reuses DEVICE_INSTANCE_NAME from secrets.h for mDNS, so the
   display is reachable at http://<DEVICE_INSTANCE_NAME>.local/ in addition to
   its raw IP (shown on the T-Display and logged over serial).

   Enable by setting -DWEB_UI=true in platformio.ini (see env:chainlink).

   API (all same-origin, consumed by web_ui_page.h):
     GET    /api/state              live module state (incl. sensor/home diagnostics), wifi info, current time
     POST   /api/message            {"text": "..."} -> shows immediately
     GET    /api/presets            [{id, name, text}, ...]
     POST   /api/presets            {"name": "...", "text": "..."} -> adds one
     DELETE /api/presets?id=NNN     removes one
     POST   /api/presets/show?id=NNN  shows a saved preset immediately
     GET    /api/automation         {mode, rotateIntervalMinutes, scheduleEntries}
     POST   /api/automation         replaces the whole automation config
     GET    /api/timer              {running, isCountdown, targetSeconds, elapsedSeconds, remainingSeconds}
     POST   /api/timer              {"action": "start", "type": "countdown"|"countup", "durationMinutes": N}
                                     {"action": "pause"} / {"action": "resume"} / {"action": "reset"}
     POST   /api/calibrate/start    {"module": N} -> claims the display for calibration (pauses
                                     automation/timer/manual-message so nothing else drives the
                                     flaps mid-calibration); fails with 409 if another module is
                                     already being calibrated
     POST   /api/calibrate/move     {"module": N, "targetFlapIndex": N} -> commands module N only
                                     to a specific flap index (0 = blank); poll /api/state to see
                                     when the move completes
     POST   /api/calibrate/setOffset {"module": N} -> locks the module's current physical position
                                     in as its new home/blank offset (not yet saved to flash)
     POST   /api/calibrate/nudgeOffset {"module": N, "tenths": N} -> shifts module N's saved
                                     offset forward by N tenths of a flap (10 = one full flap;
                                     causes a small immediate move as the new target is recomputed).
                                     For compensating a module with a consistent, repeatable
                                     under/over-shoot on every commanded move (see
                                     [[denver3d-module-expansion]]/[[bezek-labs-split-flap-display]]
                                     notes on module 3) -- NOT a substitute for the guided
                                     start/blank/verify calibration flow above. Requires an active
                                     session for the module (via /api/calibrate/start) same as move.
     POST   /api/calibrate/clearOffset {"module": N} -> resets module N's offset back to 0
                                     (uncalibrated) and immediately persists to flash, so a reboot
                                     won't reload the old saved offset. For deliberately returning a
                                     module to its out-of-the-box state (e.g. to redo/record the
                                     calibration flow), not part of normal calibration. Requires an
                                     active session for the module (via /api/calibrate/start).
     POST   /api/calibrate/save     -> persists all modules' offsets to flash and ends the
                                     calibration session (same underlying save as calibrate_single.py)
     POST   /api/calibrate/cancel   {"module": N} -> aborts the in-progress calibration session
                                     for module N without saving, releasing the display
     POST   /api/reset              -> reboots the ESP32 itself (ESP.restart()), the software
                                     equivalent of unplugging/replugging -- clears all firmware
                                     state, same as a real power cycle. WiFi/server will be
                                     briefly unreachable while it reboots and reconnects.

   Automation modes (mutually exclusive, one active at a time):
     off       - no automatic behavior; manual send/preset-show always works
     clock     - shows the current HH:MM every minute (displayed as 12-hour HH-MMAM/PM)
     date      - shows the current date as MM-DD-YY, refreshed when the date changes (midnight)
     rotate    - cycles through all saved presets every rotateIntervalMinutes
     schedule  - scheduleEntries: [{time: "HH:MM", presetId, enabled}], each
                 fires its preset once when the clock hits that time

   Timer/stopwatch: independent of the automation modes above -- while a timer
   is running (or holding its finished/paused value), it takes over the
   display; automation resumes once the timer is reset. Updates once a minute
   (not every second) to avoid constantly cycling the physical flaps, and a
   finished countdown holds at "00-00" rather than reverting immediately.
*/
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include "../core/logger.h"
#include "../core/splitflap_task.h"
#include "../core/task.h"

#include "display_task.h"

class WebUiTask : public Task<WebUiTask> {
    friend class Task<WebUiTask>; // Allow base Task to invoke protected run()

    public:
        WebUiTask(SplitflapTask& splitflap_task, DisplayTask& display_task, Logger& logger, const uint8_t task_core);

    protected:
        void run();

    private:
        SplitflapTask& splitflap_task_;
        DisplayTask& display_task_;
        Logger& logger_;
        WebServer server_;
        Preferences prefs_;

        // Persisted (JSON strings), loaded/saved via Preferences (NVS), independent
        // of the module-offset config storage used elsewhere in the firmware.
        String presets_json_ = "[]";
        String automation_json_ = "{\"mode\":\"off\",\"rotateIntervalMinutes\":10,\"scheduleEntries\":[]}";

        // Automation runtime tracking (reset whenever automation config changes)
        uint16_t rotate_index_ = 0;
        uint32_t last_rotate_millis_ = 0;
        String last_clock_shown_ = "";
        String last_date_shown_ = "";
        String last_schedule_fire_minute_ = "";

        // Timer/stopwatch runtime state. Not persisted (intentionally ephemeral,
        // like an interactive kitchen timer -- resets on reboot).
        bool timer_has_result_ = false;   // true from "start" until "reset"; claims the display
        bool timer_running_ = false;      // false while paused, or after a countdown hits zero
        bool timer_is_countdown_ = false; // false = count-up/stopwatch
        uint32_t timer_target_seconds_ = 0;
        uint32_t timer_start_millis_ = 0;
        uint32_t timer_accumulated_seconds_ = 0;
        String last_timer_display_ = "";

        // Calibration session state (ephemeral, not persisted). While active,
        // automation/timer ticks and manual message/preset sends are paused so
        // nothing else drives the flaps mid-calibration -- see run() and
        // showText().
        bool calibration_active_ = false;
        int8_t calibration_module_ = -1;

        void connectWifi();
        void loadPersistedState();
        void savePresets();
        void saveAutomation();

        void showText(const String& text);

        void handleRoot();
        void handleGetState();
        void handlePostMessage();
        void handleGetPresets();
        void handlePostPresets();
        void handleDeletePreset();
        void handleShowPreset();
        void handleGetAutomation();
        void handlePostAutomation();
        void handleGetTimer();
        void handlePostTimer();
        void handleCalibrateStart();
        void handleCalibrateMove();
        void handleCalibrateSetOffset();
        void handleCalibrateClearOffset();
        void handleCalibrateNudgeOffset();
        void handleCalibrateSave();
        void handleCalibrateCancel();
        void handleReset();
        void handleNotFound();

        void runAutomationTick();
        // Returns true if the timer currently owns the display (running, paused,
        // or holding a finished countdown at 00-00) -- callers should skip normal
        // automation-mode handling in that case.
        bool runTimerTick();
};
