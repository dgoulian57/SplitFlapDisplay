/*
   See web_ui_task.h for an overview of the API and automation modes.
*/
#if WEB_UI
#include "web_ui_task.h"

#include <ESPmDNS.h>
#include <time.h>
#include <json11.hpp>

#include "secrets.h"
#include "web_ui_page.h"

using namespace json11;

// Arizona (Rick's build location) does not observe daylight saving, so a plain
// fixed offset is correct year-round. If you build this for a location that
// does observe DST, use a POSIX TZ string with DST rules instead (see
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).
#define WEB_UI_TIMEZONE "MST7"

WebUiTask::WebUiTask(SplitflapTask& splitflap_task, DisplayTask& display_task, Logger& logger, const uint8_t task_core) :
        Task("WebUI", 10240, 1, task_core),
        splitflap_task_(splitflap_task),
        display_task_(display_task),
        logger_(logger),
        server_(80) {
}

void WebUiTask::connectWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // Disable WiFi sleep as it causes glitches on pin 39; see https://github.com/espressif/arduino-esp32/issues/4903#issuecomment-793187707
    WiFi.setSleep(WIFI_PS_NONE);

    char buf[256];
    snprintf(buf, sizeof(buf), "WiFi connecting to %s", WIFI_SSID);
    logger_.log(buf);
    display_task_.setMessage(0, String(buf));

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    snprintf(buf, sizeof(buf), "WiFi IP: %s", WiFi.localIP().toString().c_str());
    logger_.log(buf);
    display_task_.setMessage(0, String(buf));

    if (MDNS.begin(DEVICE_INSTANCE_NAME)) {
        MDNS.addService("http", "tcp", 80);
        snprintf(buf, sizeof(buf), "Web UI: http://%s.local/", DEVICE_INSTANCE_NAME);
    } else {
        logger_.log("mDNS setup failed; falling back to raw IP only");
        snprintf(buf, sizeof(buf), "Web UI: http://%s/", WiFi.localIP().toString().c_str());
    }
    logger_.log(buf);
    display_task_.setMessage(1, String(buf));

    // Sync time via NTP so clock/schedule automation modes work. Non-fatal if
    // it times out -- those modes just won't fire accurately until it succeeds
    // in the background.
    configTzTime(WEB_UI_TIMEZONE, "pool.ntp.org", "time.nist.gov");
    logger_.log("Waiting for NTP time sync...");
    time_t now = time(nullptr);
    uint32_t wait_start = millis();
    while (now < 1700000000 && millis() - wait_start < 15000) {
        delay(250);
        now = time(nullptr);
    }
    logger_.log(now >= 1700000000 ? "NTP time synced" : "NTP sync timed out; will keep retrying in background");
}

void WebUiTask::loadPersistedState() {
    prefs_.begin("webui", false);
    presets_json_ = prefs_.getString("presets", "[]");
    automation_json_ = prefs_.getString("automation", automation_json_);
}

void WebUiTask::savePresets() {
    prefs_.putString("presets", presets_json_);
}

void WebUiTask::saveAutomation() {
    prefs_.putString("automation", automation_json_);
}

void WebUiTask::showText(const String& text) {
    if (calibration_active_) {
        // Defensive no-op: a calibration session should already block callers
        // (see handlePostMessage/handleShowPreset and the automation-tick skip
        // in run()), but this is a last line of defense against anything
        // driving all modules while one is mid-calibration.
        return;
    }
    String truncated = text;
    if (truncated.length() > NUM_MODULES) {
        truncated = truncated.substring(0, NUM_MODULES);
    }
    splitflap_task_.showString(truncated.c_str(), truncated.length(), false, true);

    char buf[128];
    snprintf(buf, sizeof(buf), "Web UI: showing \"%s\"", truncated.c_str());
    logger_.log(buf);
}

void WebUiTask::handleRoot() {
    server_.send_P(200, "text/html", WEB_UI_HTML);
}

namespace {
    // Mirrors SerialLegacyJsonProtocol::dumpStatus's state names (serial_legacy_json_protocol.cpp)
    // so USB and wireless diagnostics agree.
    std::string moduleStateName(State state) {
        switch (state) {
            case NORMAL:
                return "normal";
            case LOOK_FOR_HOME:
                return "look_for_home";
            case SENSOR_ERROR:
                return "sensor_error";
            case PANIC:
                return "panic";
            case STATE_DISABLED:
                return "disabled";
            default:
                return "unknown";
        }
    }
}

void WebUiTask::handleGetState() {
    SplitflapState state = splitflap_task_.getState();
    std::vector<Json> modules_arr;
    for (uint8_t i = 0; i < NUM_MODULES; i++) {
        char c = (char)flaps[state.modules[i].flap_index];
        modules_arr.push_back(Json::object{
            {"flapIndex", state.modules[i].flap_index},
            {"char", std::string(1, c)},
            {"moving", state.modules[i].moving},
            {"state", moduleStateName(state.modules[i].state)},
            {"homeState", state.modules[i].home_state},
            {"countMissedHome", state.modules[i].count_missed_home},
            {"countUnexpectedHome", state.modules[i].count_unexpected_home},
        });
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    Json response = Json::object{
        {"numModules", NUM_MODULES},
        {"modules", modules_arr},
        {"wifiConnected", WiFi.status() == WL_CONNECTED},
        {"ip", std::string(WiFi.localIP().toString().c_str())},
        {"time", std::string(time_buf)},
    };
    server_.send(200, "application/json", response.dump().c_str());
}

void WebUiTask::handlePostMessage() {
    if (calibration_active_) {
        server_.send(409, "application/json", "{\"error\":\"calibration in progress; finish or cancel it first\"}");
        return;
    }
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["text"].is_string()) {
        server_.send(400, "application/json", "{\"error\":\"expected {text: string}\"}");
        return;
    }
    showText(String(body["text"].string_value().c_str()));
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleGetPresets() {
    server_.send(200, "application/json", presets_json_);
}

void WebUiTask::handlePostPresets() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["name"].is_string() || !body["text"].is_string()) {
        server_.send(400, "application/json", "{\"error\":\"expected {name, text}\"}");
        return;
    }

    Json existing = Json::parse(presets_json_.c_str(), err);
    std::vector<Json> items = existing.array_items();
    uint32_t new_id = millis();
    items.push_back(Json::object{
        {"id", (double)new_id},
        {"name", body["name"].string_value()},
        {"text", body["text"].string_value()},
    });
    presets_json_ = String(Json(items).dump().c_str());
    savePresets();
    server_.send(200, "application/json", presets_json_);
}

void WebUiTask::handleDeletePreset() {
    if (!server_.hasArg("id")) {
        server_.send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }
    long id = server_.arg("id").toInt();
    std::string err;
    Json existing = Json::parse(presets_json_.c_str(), err);
    std::vector<Json> filtered;
    for (auto &item : existing.array_items()) {
        if ((long)item["id"].number_value() != id) {
            filtered.push_back(item);
        }
    }
    presets_json_ = String(Json(filtered).dump().c_str());
    savePresets();
    server_.send(200, "application/json", presets_json_);
}

void WebUiTask::handleShowPreset() {
    if (calibration_active_) {
        server_.send(409, "application/json", "{\"error\":\"calibration in progress; finish or cancel it first\"}");
        return;
    }
    if (!server_.hasArg("id")) {
        server_.send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }
    long id = server_.arg("id").toInt();
    std::string err;
    Json existing = Json::parse(presets_json_.c_str(), err);
    for (auto &item : existing.array_items()) {
        if ((long)item["id"].number_value() == id) {
            showText(String(item["text"].string_value().c_str()));
            server_.send(200, "application/json", "{\"ok\":true}");
            return;
        }
    }
    server_.send(404, "application/json", "{\"error\":\"not found\"}");
}

void WebUiTask::handleGetAutomation() {
    server_.send(200, "application/json", automation_json_);
}

void WebUiTask::handlePostAutomation() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["mode"].is_string()) {
        server_.send(400, "application/json", "{\"error\":\"expected {mode, ...}\"}");
        return;
    }
    automation_json_ = String(body.dump().c_str());
    saveAutomation();

    // Reset runtime tracking so the new config takes effect immediately rather
    // than waiting for stale rotate/clock/schedule state to roll over.
    rotate_index_ = 0;
    last_rotate_millis_ = 0;
    last_clock_shown_ = "";
    last_date_shown_ = "";
    last_schedule_fire_minute_ = "";

    server_.send(200, "application/json", automation_json_);
}

void WebUiTask::handleGetTimer() {
    uint32_t elapsed_seconds = timer_accumulated_seconds_;
    if (timer_running_) {
        elapsed_seconds += (millis() - timer_start_millis_) / 1000;
    }
    uint32_t remaining_seconds = 0;
    if (timer_is_countdown_) {
        remaining_seconds = (elapsed_seconds >= timer_target_seconds_) ? 0 : (timer_target_seconds_ - elapsed_seconds);
    }
    Json response = Json::object{
        {"hasResult", timer_has_result_},
        {"running", timer_running_},
        {"isCountdown", timer_is_countdown_},
        {"targetSeconds", (double)timer_target_seconds_},
        {"elapsedSeconds", (double)elapsed_seconds},
        {"remainingSeconds", (double)remaining_seconds},
    };
    server_.send(200, "application/json", response.dump().c_str());
}

void WebUiTask::handlePostTimer() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["action"].is_string()) {
        server_.send(400, "application/json", "{\"error\":\"expected {action, ...}\"}");
        return;
    }
    std::string action = body["action"].string_value();

    if (action == "start") {
        timer_is_countdown_ = body["type"].string_value() == "countdown";
        timer_target_seconds_ = timer_is_countdown_
            ? (uint32_t)(body["durationMinutes"].number_value() * 60.0)
            : 0;
        timer_accumulated_seconds_ = 0;
        timer_start_millis_ = millis();
        timer_running_ = true;
        timer_has_result_ = true;
        last_timer_display_ = ""; // force an immediate redraw on the next tick
    } else if (action == "pause") {
        if (timer_running_) {
            timer_accumulated_seconds_ += (millis() - timer_start_millis_) / 1000;
            timer_running_ = false;
        }
    } else if (action == "resume") {
        if (timer_has_result_ && !timer_running_) {
            timer_start_millis_ = millis();
            timer_running_ = true;
        }
    } else if (action == "reset") {
        timer_has_result_ = false;
        timer_running_ = false;
        timer_accumulated_seconds_ = 0;
        timer_target_seconds_ = 0;
        last_timer_display_ = "";
    } else {
        server_.send(400, "application/json", "{\"error\":\"unknown action\"}");
        return;
    }

    handleGetTimer();
}

void WebUiTask::handleCalibrateStart() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["module"].is_number()) {
        server_.send(400, "application/json", "{\"error\":\"expected {module: number}\"}");
        return;
    }
    int module = (int)body["module"].number_value();
    if (module < 0 || module >= NUM_MODULES) {
        server_.send(400, "application/json", "{\"error\":\"module out of range\"}");
        return;
    }
    if (calibration_active_ && calibration_module_ != module) {
        server_.send(409, "application/json", "{\"error\":\"another module is already being calibrated\"}");
        return;
    }
    calibration_active_ = true;
    calibration_module_ = (int8_t)module;
    char buf[64];
    snprintf(buf, sizeof(buf), "Web UI: starting calibration for module %d", module);
    logger_.log(buf);
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleCalibrateMove() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["module"].is_number() || !body["targetFlapIndex"].is_number()) {
        server_.send(400, "application/json", "{\"error\":\"expected {module, targetFlapIndex}\"}");
        return;
    }
    int module = (int)body["module"].number_value();
    int target = (int)body["targetFlapIndex"].number_value();
    if (!calibration_active_ || module != calibration_module_) {
        server_.send(409, "application/json", "{\"error\":\"no active calibration session for this module\"}");
        return;
    }
    if (module < 0 || module >= NUM_MODULES || target < 0 || target >= NUM_FLAPS) {
        server_.send(400, "application/json", "{\"error\":\"module or targetFlapIndex out of range\"}");
        return;
    }

    // Same underlying mechanism as setOffset()/showString() -- a MODULES
    // command that only sets this one module's slot, leaving the rest at
    // QCMD_NO_OP (0, from the zero-initialized Command) so no other module is
    // disturbed.
    Command command = {};
    command.command_type = CommandType::MODULES;
    command.data.module_command[module] = QCMD_FLAP + target;
    splitflap_task_.postRawCommand(command);
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleCalibrateSetOffset() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["module"].is_number()) {
        server_.send(400, "application/json", "{\"error\":\"expected {module: number}\"}");
        return;
    }
    int module = (int)body["module"].number_value();
    if (!calibration_active_ || module != calibration_module_) {
        server_.send(409, "application/json", "{\"error\":\"no active calibration session for this module\"}");
        return;
    }
    splitflap_task_.setOffset((uint8_t)module);
    char buf[64];
    snprintf(buf, sizeof(buf), "Web UI: SET_OFFSET for module %d", module);
    logger_.log(buf);
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleCalibrateClearOffset() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["module"].is_number()) {
        server_.send(400, "application/json", "{\"error\":\"expected {module: number}\"}");
        return;
    }
    int module = (int)body["module"].number_value();
    if (!calibration_active_ || module != calibration_module_) {
        server_.send(409, "application/json", "{\"error\":\"no active calibration session for this module\"}");
        return;
    }
    splitflap_task_.clearOffset((uint8_t)module);
    // Persist immediately (unlike setOffset/nudgeOffset, which wait for the
    // normal wizard's explicit save step) -- the whole point of this endpoint
    // is to return the module to an uncalibrated state that survives a
    // reboot, not just clear it in memory until the next power cycle reloads
    // the old saved offset from flash.
    splitflap_task_.saveAllOffsets();
    char buf[64];
    snprintf(buf, sizeof(buf), "Web UI: cleared calibration for module %d", module);
    logger_.log(buf);
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleCalibrateNudgeOffset() {
    if (!server_.hasArg("plain")) {
        server_.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    std::string err;
    Json body = Json::parse(server_.arg("plain").c_str(), err);
    if (!err.empty() || !body["module"].is_number() || !body["tenths"].is_number()) {
        server_.send(400, "application/json", "{\"error\":\"expected {module, tenths}\"}");
        return;
    }
    int module = (int)body["module"].number_value();
    int tenths = (int)body["tenths"].number_value();
    if (!calibration_active_ || module != calibration_module_) {
        server_.send(409, "application/json", "{\"error\":\"no active calibration session for this module\"}");
        return;
    }
    // Bounded to at most 2 full flaps (20 tenths) per call -- this is meant for
    // a small, deliberate compensating shift (e.g. +10 = 1 flap forward to
    // compensate a module that consistently undershoots every move by one
    // flap), not general-purpose calibration. Each tenth is applied via the
    // existing IncreaseOffset mechanism (same one behind the old HALF/TENTH
    // nudge buttons) -- fine for a single one-time shift like this, even
    // though repeated interactive nudging was previously abandoned for actual
    // calibration due to accumulated rounding error over many taps.
    if (tenths < 1 || tenths > 20) {
        server_.send(400, "application/json", "{\"error\":\"tenths must be between 1 and 20\"}");
        return;
    }
    for (int i = 0; i < tenths; i++) {
        splitflap_task_.increaseOffsetTenth((uint8_t)module);
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "Web UI: nudged module %d offset by %d tenths of a flap", module, tenths);
    logger_.log(buf);
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleCalibrateSave() {
    if (!calibration_active_) {
        server_.send(409, "application/json", "{\"error\":\"no active calibration session\"}");
        return;
    }
    // Note: like calibrate_single.py, this only queues the save -- the
    // firmware logs "SUCCESS - saved calibration!" (or an error) asynchronously.
    // The client is expected to only call this after confirming the module is
    // idle at blank (same precondition the Python script enforces), so the
    // "module isn't idle" failure path in SplitflapTask shouldn't trigger here.
    splitflap_task_.saveAllOffsets();
    logger_.log("Web UI: calibration save requested");
    calibration_active_ = false;
    calibration_module_ = -1;
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleCalibrateCancel() {
    calibration_active_ = false;
    calibration_module_ = -1;
    logger_.log("Web UI: calibration cancelled");
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUiTask::handleReset() {
    logger_.log("Web UI: reboot requested");
    server_.send(200, "application/json", "{\"ok\":true}");
    // Give the response time to actually go out over the socket before the
    // reboot tears everything down.
    server_.client().flush();
    delay(200);
    ESP.restart();
}

void WebUiTask::handleNotFound() {
    server_.send(404, "text/plain", "Not found");
}

bool WebUiTask::runTimerTick() {
    if (!timer_has_result_) {
        return false;
    }
    if (!timer_running_) {
        // Paused, or a finished countdown holding at 00-00 -- nothing to
        // recompute or redraw, just keep claiming the display.
        return true;
    }

    uint32_t elapsed_seconds = timer_accumulated_seconds_ + (millis() - timer_start_millis_) / 1000;
    uint32_t total_minutes;

    if (timer_is_countdown_) {
        uint32_t remaining_seconds = (elapsed_seconds >= timer_target_seconds_) ? 0 : (timer_target_seconds_ - elapsed_seconds);
        // Ceiling, so "1 minute remaining" keeps showing until it's truly at
        // zero (a plain floor would jump to 00-00 up to 59s early).
        total_minutes = remaining_seconds > 0 ? (remaining_seconds + 59) / 60 : 0;
        if (remaining_seconds == 0) {
            timer_running_ = false; // hold at 00-00 per Rick's spec
        }
    } else {
        // Stopwatch: floor, i.e. "1 minute elapsed" once a full minute has passed.
        total_minutes = elapsed_seconds / 60;
    }

    uint32_t hh = total_minutes / 60;
    uint32_t mm = total_minutes % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu-%02lu", (unsigned long)hh, (unsigned long)mm);
    String display(buf);
    if (display != last_timer_display_) {
        last_timer_display_ = display;
        showText(display);
    }
    return true;
}

void WebUiTask::runAutomationTick() {
    // The timer/stopwatch takes precedence over clock/rotate/schedule while
    // it's running, paused, or holding a finished countdown at 00-00.
    if (runTimerTick()) {
        return;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2023 - 1900)) {
        // Time not synced yet; skip clock/rotate/schedule automation until NTP catches up.
        return;
    }

    char hhmm[6];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &timeinfo);
    String current_hhmm(hhmm);

    std::string err;
    Json automation = Json::parse(automation_json_.c_str(), err);
    if (!err.empty()) {
        return;
    }
    std::string mode = automation["mode"].string_value();

    if (mode == "clock") {
        // current_hhmm (24-hour, colon-separated) is only used here to detect
        // when the minute has rolled over -- it's never shown on the flaps.
        if (current_hhmm != last_clock_shown_) {
            last_clock_shown_ = current_hhmm;

            // Actual display: 12-hour with AM/PM instead of 24-hour, since the
            // flap set (config.h) has uppercase A/M/P but no colon. %p already
            // yields "AM"/"PM" in uppercase, e.g. "08-45PM".
            char clock_buf[12];
            strftime(clock_buf, sizeof(clock_buf), "%I-%M%p", &timeinfo);
            showText(String(clock_buf));
        }
        return;
    }

    if (mode == "date") {
        // MM-DD-YY, e.g. "07-25-26". Only redraw when the date actually
        // changes (i.e. once at midnight), not on every automation tick.
        char date_buf[12];
        strftime(date_buf, sizeof(date_buf), "%m-%d-%y", &timeinfo);
        String current_date(date_buf);
        if (current_date != last_date_shown_) {
            last_date_shown_ = current_date;
            showText(current_date);
        }
        return;
    }

    if (mode == "rotate") {
        uint32_t interval_ms = (uint32_t)(automation["rotateIntervalMinutes"].number_value() * 60000.0);
        if (interval_ms == 0) {
            interval_ms = 600000UL;
        }
        uint32_t now_millis = millis();
        Json presets = Json::parse(presets_json_.c_str(), err);
        auto items = presets.array_items();
        if (items.size() == 0) {
            return;
        }
        if (last_rotate_millis_ == 0 || now_millis - last_rotate_millis_ >= interval_ms) {
            last_rotate_millis_ = now_millis;
            if (rotate_index_ >= items.size()) {
                rotate_index_ = 0;
            }
            showText(String(items[rotate_index_]["text"].string_value().c_str()));
            rotate_index_++;
        }
        return;
    }

    if (mode == "schedule") {
        if (current_hhmm == last_schedule_fire_minute_) {
            // Already handled this minute.
            return;
        }
        last_schedule_fire_minute_ = current_hhmm;

        auto entries = automation["scheduleEntries"].array_items();
        for (auto &entry : entries) {
            if (!entry["enabled"].bool_value()) {
                continue;
            }
            if (String(entry["time"].string_value().c_str()) != current_hhmm) {
                continue;
            }
            long preset_id = (long)entry["presetId"].number_value();
            Json presets = Json::parse(presets_json_.c_str(), err);
            for (auto &p : presets.array_items()) {
                if ((long)p["id"].number_value() == preset_id) {
                    showText(String(p["text"].string_value().c_str()));
                    break;
                }
            }
            break; // only one scheduled entry can fire per minute
        }
        return;
    }
    // mode == "off" (or unrecognized): do nothing automatically.
}

void WebUiTask::run() {
    loadPersistedState();
    connectWifi();

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/state", HTTP_GET, [this]() { handleGetState(); });
    server_.on("/api/message", HTTP_POST, [this]() { handlePostMessage(); });
    server_.on("/api/presets", HTTP_GET, [this]() { handleGetPresets(); });
    server_.on("/api/presets", HTTP_POST, [this]() { handlePostPresets(); });
    server_.on("/api/presets", HTTP_DELETE, [this]() { handleDeletePreset(); });
    server_.on("/api/presets/show", HTTP_POST, [this]() { handleShowPreset(); });
    server_.on("/api/automation", HTTP_GET, [this]() { handleGetAutomation(); });
    server_.on("/api/automation", HTTP_POST, [this]() { handlePostAutomation(); });
    server_.on("/api/timer", HTTP_GET, [this]() { handleGetTimer(); });
    server_.on("/api/timer", HTTP_POST, [this]() { handlePostTimer(); });
    server_.on("/api/calibrate/start", HTTP_POST, [this]() { handleCalibrateStart(); });
    server_.on("/api/calibrate/move", HTTP_POST, [this]() { handleCalibrateMove(); });
    server_.on("/api/calibrate/setOffset", HTTP_POST, [this]() { handleCalibrateSetOffset(); });
    server_.on("/api/calibrate/clearOffset", HTTP_POST, [this]() { handleCalibrateClearOffset(); });
    server_.on("/api/calibrate/nudgeOffset", HTTP_POST, [this]() { handleCalibrateNudgeOffset(); });
    server_.on("/api/calibrate/save", HTTP_POST, [this]() { handleCalibrateSave(); });
    server_.on("/api/calibrate/cancel", HTTP_POST, [this]() { handleCalibrateCancel(); });
    server_.on("/api/reset", HTTP_POST, [this]() { handleReset(); });
    server_.onNotFound([this]() { handleNotFound(); });
    server_.begin();
    logger_.log("Web UI server started");

    uint32_t last_tick = 0;
    while (1) {
        server_.handleClient();

        uint32_t now = millis();
        if (now - last_tick >= 1000) {
            last_tick = now;
            if (!calibration_active_) {
                runAutomationTick();
            }
        }
        delay(2);
    }
}

#endif
