/*
   Wireless web UI for the split-flap display.

   Single self-contained HTML/CSS/JS page served directly by the ESP32 (no
   external assets, no build step). Talks to the on-device JSON API
   implemented in web_ui_task.cpp:
     GET  /api/state             -> live module state
     POST /api/message            {text}
     GET  /api/presets
     POST /api/presets            {name, text}
     DELETE /api/presets?id=...
     POST /api/presets/show?id=...
     GET  /api/automation
     POST /api/automation          {mode, rotateIntervalMinutes, scheduleEntries}
     POST /api/calibrate/start     {module}
     POST /api/calibrate/move      {module, targetFlapIndex}
     POST /api/calibrate/setOffset {module}
     POST /api/calibrate/save
     POST /api/calibrate/cancel    {module}
     POST /api/reset                reboots the ESP32 (software equivalent of power-cycling)
*/
#pragma once

#include <Arduino.h>

const char WEB_UI_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Split Flap Display</title>
<style>
  body { font-family: -apple-system, system-ui, sans-serif; background:#111; color:#eee; margin:0; padding:16px; }
  h1 { font-size:1.2rem; margin:0 0 12px; }
  .card { background:#1c1c1c; border-radius:10px; padding:14px; margin-bottom:16px; }
  .display-row { display:flex; gap:4px; flex-wrap:wrap; margin-bottom:8px; }
  .flap { width:28px; height:36px; background:#000; color:#0f0; font-family:monospace; font-size:1.1rem; display:flex; align-items:center; justify-content:center; border-radius:3px; border:1px solid #333; }
  .flap.moving { border-color:#f80; }
  input, select, button { font-size:1rem; padding:8px; border-radius:6px; border:1px solid #444; background:#2a2a2a; color:#eee; }
  button { background:#3a6df0; border:none; cursor:pointer; }
  button.secondary { background:#444; }
  button.danger { background:#a33; }
  .row { display:flex; gap:8px; margin-bottom:8px; flex-wrap:wrap; }
  .preset { display:flex; justify-content:space-between; align-items:center; padding:6px 0; border-bottom:1px solid #333; gap:8px; }
  .muted { color:#999; font-size:0.85rem; }
  label { display:block; margin:8px 0 4px; font-size:0.85rem; color:#aaa; }
  .sched-entry { display:flex; gap:6px; align-items:center; margin-bottom:6px; }
  .cal-step { padding:10px 0; }
  .cal-status { margin:8px 0; font-size:0.9rem; }
  .cal-status.warn { color:#e0a030; }
  .cal-status.err { color:#e05050; }
  .cal-status.ok { color:#4caf50; }
</style>
</head>
<body>
  <h1>Split Flap Display</h1>

  <div class="card">
    <div id="displayRow" class="display-row"></div>
    <div class="row" style="align-items:center; justify-content:space-between;">
      <div class="muted" id="statusLine">Loading...</div>
      <button class="secondary" onclick="restartDisplay()">Restart display</button>
    </div>
  </div>

  <div class="card">
    <label>Send a message</label>
    <div class="row">
      <input type="text" id="messageInput" placeholder="Type a message" style="flex:1;">
      <button onclick="sendMessage()">Send</button>
    </div>
    <div class="row">
      <input type="text" id="presetName" placeholder="Preset name (optional save)" style="flex:1;">
      <button class="secondary" onclick="saveAsPreset()">Save as preset</button>
    </div>
  </div>

  <div class="card">
    <label>Presets</label>
    <div id="presetList"></div>
  </div>

  <div class="card">
    <label>Timer</label>
    <div class="row">
      <select id="timerType">
        <option value="countdown">Countdown</option>
        <option value="countup">Stopwatch (count up)</option>
      </select>
      <input type="number" id="timerMinutes" min="1" value="5" placeholder="Minutes" style="width:90px;">
    </div>
    <div class="row">
      <button onclick="startTimer()">Start</button>
      <button class="secondary" id="timerPauseBtn" onclick="timerPauseResume()">Pause</button>
      <button class="danger" onclick="timerAction('reset')">Reset</button>
    </div>
    <div class="muted" id="timerStatus">Not running</div>
    <div class="muted">Updates once a minute (not live seconds) to avoid constantly cycling the flaps. A finished countdown holds at 00-00 until reset.</div>
  </div>

  <div class="card">
    <label>Automation</label>
    <select id="modeSelect" onchange="onModeChange()">
      <option value="off">Off</option>
      <option value="clock">Show clock (12-hour, e.g. 08-45PM)</option>
      <option value="date">Show date (e.g. 07-25-26)</option>
      <option value="rotate">Rotate through presets</option>
      <option value="schedule">Scheduled times</option>
    </select>

    <div id="rotateConfig" style="display:none;">
      <label>Rotate interval (minutes)</label>
      <input type="number" id="rotateInterval" min="1" value="10">
    </div>

    <div id="scheduleConfig" style="display:none;">
      <div id="scheduleEntries"></div>
      <button class="secondary" onclick="addScheduleRow()">+ Add time</button>
    </div>

    <div class="row" style="margin-top:10px;">
      <button onclick="saveAutomation()">Save automation settings</button>
    </div>
  </div>

  <div class="card">
    <label>Calibration</label>
    <div class="muted">Guided calibration for a single module, over WiFi -- no laptop/USB needed. Same verified flow as calibrate_single.py: move to blank, you visually confirm, verify 'A', confirm, save.</div>

    <div id="calIdle" class="cal-step">
      <div class="row">
        <select id="calModuleSelect"></select>
        <button onclick="calStart()">Start calibration</button>
      </div>
    </div>

    <div id="calWizard" class="cal-step" style="display:none;">
      <div class="muted" id="calModuleLabel"></div>

      <div id="calAskChar">
        <label>What is this module showing right now?</label>
        <div class="row">
          <select id="calCurrentChar"></select>
          <button onclick="calMoveToBlank()">Move to blank</button>
          <button class="secondary" onclick="calCancel()">Cancel</button>
        </div>
      </div>

      <div id="calConfirmBlank" style="display:none;">
        <div class="cal-status" id="calBlankStatus">Moving...</div>
        <div class="row" id="calBlankConfirmRow" style="display:none;">
          <span>Does the module look ACTUALLY blank right now?</span>
          <button onclick="calBlankConfirmed(true)">Yes</button>
          <button class="danger" onclick="calBlankConfirmed(false)">No</button>
        </div>
      </div>

      <div id="calConfirmA" style="display:none;">
        <div class="cal-status" id="calAStatus">Moving...</div>
        <div class="row" id="calAConfirmRow" style="display:none;">
          <span>Does the module show 'A'?</span>
          <button onclick="calAConfirmed(true)">Yes</button>
          <button class="danger" onclick="calAConfirmed(false)">No</button>
        </div>
      </div>

      <div id="calSaving" style="display:none;">
        <div class="cal-status" id="calSaveStatus">Returning to blank and saving...</div>
      </div>

      <div id="calResult" style="display:none;">
        <div class="cal-status" id="calResultStatus"></div>
        <div class="row">
          <button class="secondary" onclick="calReset()">Done</button>
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <label>Offset Nudge (advanced)</label>
    <div class="muted">For a module that's already roughly calibrated but consistently lands a flap or two short/over on every move. Shifts the saved offset by a small amount instead of a full recalibration.</div>

    <div id="nudgeIdle" class="cal-step">
      <div class="row">
        <select id="nudgeModuleSelect"></select>
        <input type="number" id="nudgeTenths" value="10" min="1" max="20" style="width:80px;" title="10 = one full flap">
        <button onclick="nudgeApply()">Start &amp; apply nudge</button>
      </div>
      <div class="muted">Tenths of a flap (10 = 1 full flap forward).</div>
    </div>

    <div id="nudgeActive" class="cal-step" style="display:none;">
      <div class="muted" id="nudgeLabel"></div>
      <div class="row">
        <button onclick="nudgeTestBlank()">Move to blank &amp; check</button>
        <button class="secondary" onclick="nudgeAgain()">Nudge again</button>
        <button class="danger" onclick="nudgeCancel()">Cancel</button>
      </div>
      <div class="cal-status" id="nudgeStatus"></div>
      <div class="row" id="nudgeConfirmRow" style="display:none;">
        <span>Does the module look ACTUALLY blank now?</span>
        <button onclick="nudgeConfirmed(true)">Yes, save</button>
        <button class="danger" onclick="nudgeConfirmed(false)">No</button>
      </div>
    </div>
  </div>

<script>
let presets = [];
let automation = { mode: "off", rotateIntervalMinutes: 10, scheduleEntries: [] };

async function refreshState() {
  try {
    const res = await fetch('/api/state');
    const state = await res.json();
    const row = document.getElementById('displayRow');
    row.innerHTML = '';
    state.modules.forEach(m => {
      const div = document.createElement('div');
      div.className = 'flap' + (m.moving ? ' moving' : '');
      div.textContent = m.char === ' ' ? ' ' : m.char;
      row.appendChild(div);
    });
    document.getElementById('statusLine').textContent =
      (state.wifiConnected ? 'Connected: ' + state.ip : 'Wifi disconnected') + ' • ' + state.time;
    if (state.numModules && state.numModules !== calNumModules) {
      calNumModules = state.numModules;
      populateCalSelectors();
      populateNudgeSelector();
    }
  } catch (e) {
    document.getElementById('statusLine').textContent = 'Unable to reach display';
  }
}

async function sendMessage() {
  const text = document.getElementById('messageInput').value;
  if (!text) return;
  await fetch('/api/message', { method: 'POST', body: JSON.stringify({ text }) });
}

async function saveAsPreset() {
  const text = document.getElementById('messageInput').value;
  const name = document.getElementById('presetName').value || text;
  if (!text) return;
  await fetch('/api/presets', { method: 'POST', body: JSON.stringify({ name, text }) });
  document.getElementById('presetName').value = '';
  loadPresets();
}

async function loadPresets() {
  const res = await fetch('/api/presets');
  presets = await res.json();
  renderPresets();
  renderScheduleEntries();
}

function renderPresets() {
  const list = document.getElementById('presetList');
  list.innerHTML = '';
  if (presets.length === 0) {
    list.innerHTML = '<div class="muted">No presets saved yet.</div>';
    return;
  }
  presets.forEach(p => {
    const row = document.createElement('div');
    row.className = 'preset';
    const info = document.createElement('div');
    info.innerHTML = '<strong>' + escapeHtml(p.name) + '</strong><div class="muted">' + escapeHtml(p.text) + '</div>';
    const btns = document.createElement('div');
    const showBtn = document.createElement('button');
    showBtn.className = 'secondary';
    showBtn.textContent = 'Show';
    showBtn.onclick = () => fetch('/api/presets/show?id=' + p.id, { method: 'POST' });
    const delBtn = document.createElement('button');
    delBtn.className = 'danger';
    delBtn.textContent = 'Delete';
    delBtn.style.marginLeft = '6px';
    delBtn.onclick = async () => { await fetch('/api/presets?id=' + p.id, { method: 'DELETE' }); loadPresets(); };
    btns.appendChild(showBtn);
    btns.appendChild(delBtn);
    row.appendChild(info);
    row.appendChild(btns);
    list.appendChild(row);
  });
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

async function loadAutomation() {
  const res = await fetch('/api/automation');
  automation = await res.json();
  automation.scheduleEntries = automation.scheduleEntries || [];
  document.getElementById('modeSelect').value = automation.mode || 'off';
  document.getElementById('rotateInterval').value = automation.rotateIntervalMinutes || 10;
  onModeChange();
  renderScheduleEntries();
}

function onModeChange() {
  const mode = document.getElementById('modeSelect').value;
  document.getElementById('rotateConfig').style.display = mode === 'rotate' ? 'block' : 'none';
  document.getElementById('scheduleConfig').style.display = mode === 'schedule' ? 'block' : 'none';
}

function renderScheduleEntries() {
  const container = document.getElementById('scheduleEntries');
  container.innerHTML = '';
  const entries = automation.scheduleEntries || [];
  entries.forEach((entry, idx) => {
    const row = document.createElement('div');
    row.className = 'sched-entry';
    const timeInput = document.createElement('input');
    timeInput.type = 'time';
    timeInput.value = entry.time || '08:00';
    timeInput.onchange = () => entries[idx].time = timeInput.value;

    const presetSelect = document.createElement('select');
    presets.forEach(p => {
      const opt = document.createElement('option');
      opt.value = p.id;
      opt.textContent = p.name;
      if (entry.presetId == p.id) opt.selected = true;
      presetSelect.appendChild(opt);
    });
    presetSelect.onchange = () => entries[idx].presetId = parseInt(presetSelect.value);

    const enabledCb = document.createElement('input');
    enabledCb.type = 'checkbox';
    enabledCb.checked = entry.enabled !== false;
    enabledCb.onchange = () => entries[idx].enabled = enabledCb.checked;

    const rmBtn = document.createElement('button');
    rmBtn.className = 'danger';
    rmBtn.textContent = 'x';
    rmBtn.onclick = () => { entries.splice(idx, 1); renderScheduleEntries(); };

    row.appendChild(timeInput);
    row.appendChild(presetSelect);
    row.appendChild(enabledCb);
    row.appendChild(rmBtn);
    container.appendChild(row);
  });
}

function addScheduleRow() {
  automation.scheduleEntries = automation.scheduleEntries || [];
  automation.scheduleEntries.push({ time: '08:00', presetId: presets[0] ? presets[0].id : 0, enabled: true });
  renderScheduleEntries();
}

async function saveAutomation() {
  automation.mode = document.getElementById('modeSelect').value;
  automation.rotateIntervalMinutes = parseInt(document.getElementById('rotateInterval').value) || 10;
  await fetch('/api/automation', { method: 'POST', body: JSON.stringify(automation) });
  alert('Saved');
}

let timerState = {};

async function timerAction(action, extra) {
  const body = Object.assign({ action }, extra || {});
  const res = await fetch('/api/timer', { method: 'POST', body: JSON.stringify(body) });
  timerState = await res.json();
  renderTimerStatus();
}

function startTimer() {
  const type = document.getElementById('timerType').value;
  const minutes = parseInt(document.getElementById('timerMinutes').value) || 5;
  timerAction('start', { type, durationMinutes: minutes });
}

function timerPauseResume() {
  timerAction(timerState.running ? 'pause' : 'resume');
}

function formatHHMM(totalSeconds) {
  const totalMinutes = Math.floor(totalSeconds / 60);
  const hh = String(Math.floor(totalMinutes / 60)).padStart(2, '0');
  const mm = String(totalMinutes % 60).padStart(2, '0');
  return hh + '-' + mm;
}

function renderTimerStatus() {
  const el = document.getElementById('timerStatus');
  const btn = document.getElementById('timerPauseBtn');
  if (!timerState || !timerState.hasResult) {
    el.textContent = 'Not running';
    btn.textContent = 'Pause';
    return;
  }
  const shown = timerState.isCountdown ? timerState.remainingSeconds : timerState.elapsedSeconds;
  const label = timerState.isCountdown ? 'Remaining: ' : 'Elapsed: ';
  el.textContent = label + formatHHMM(shown) + (timerState.running ? '' : ' (paused)');
  btn.textContent = timerState.running ? 'Pause' : 'Resume';
}

async function refreshTimer() {
  try {
    const res = await fetch('/api/timer');
    timerState = await res.json();
    renderTimerStatus();
  } catch (e) {}
}

// ---- Calibration wizard ----
// Mirrors the verified calibrate_single.py flow (see firmware/calibrate_single.py):
//   1. Ask what the module is ACTUALLY showing (human ground truth -- firmware's
//      own tracked position can't be trusted for an uncalibrated/mis-set module).
//   2. Command it forward to blank, wait for the move to finish, ask the human
//      to visually confirm it's really blank. If not, abort without saving.
//   3. Lock that position in (SET_OFFSET), then verify by moving to 'A' and
//      asking the human to confirm. If not, abort without saving.
//   4. Return to blank and save all offsets to flash.
// Same character set as firmware/src/config.h's `flaps[]` -- keep in sync if
// that array ever changes.
const CAL_FLAPS = [
  ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
  'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
  'Z', 'g', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'r',
  '.', '?', '-', '$', '\'', '#', 'y', 'p', ',', '!', '@', '&', 'w'
];
const CAL_MOVE_TIMEOUT_MS = 20000;
const CAL_POLL_MS = 350;

let calModule = null;
let calNumModules = 12; // updated from /api/state once loaded

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function populateCalSelectors() {
  const moduleSel = document.getElementById('calModuleSelect');
  if (moduleSel.options.length !== calNumModules) {
    moduleSel.innerHTML = '';
    for (let i = 0; i < calNumModules; i++) {
      const opt = document.createElement('option');
      opt.value = i;
      opt.textContent = 'Module ' + i;
      moduleSel.appendChild(opt);
    }
  }
  const charSel = document.getElementById('calCurrentChar');
  if (charSel.options.length === 0) {
    CAL_FLAPS.forEach(c => {
      const opt = document.createElement('option');
      opt.value = c;
      opt.textContent = c === ' ' ? '(blank)' : c;
      charSel.appendChild(opt);
    });
  }
}

async function calStart() {
  const module = parseInt(document.getElementById('calModuleSelect').value);
  const res = await fetch('/api/calibrate/start', { method: 'POST', body: JSON.stringify({ module }) });
  const body = await res.json();
  if (!res.ok) {
    alert(body.error || 'Could not start calibration');
    return;
  }
  calModule = module;
  document.getElementById('calIdle').style.display = 'none';
  document.getElementById('calWizard').style.display = 'block';
  document.getElementById('calModuleLabel').textContent = 'Calibrating module ' + module;
  document.getElementById('calAskChar').style.display = 'block';
  document.getElementById('calConfirmBlank').style.display = 'none';
  document.getElementById('calConfirmA').style.display = 'none';
  document.getElementById('calSaving').style.display = 'none';
  document.getElementById('calResult').style.display = 'none';
}

async function calGetModuleState(module) {
  const res = await fetch('/api/state');
  const state = await res.json();
  return state.modules[module];
}

// Waits for the module to report idle after a move was issued. Like the
// human-confirm steps that follow, this is informational, not the final
// word -- firmware can report success even if the physical drum didn't
// actually move (a known failure mode with this hardware), which is exactly
// why every step here ends with a human visually confirming the result.
async function calWaitForIdle(module) {
  const start = Date.now();
  let sawMoving = false;
  while (Date.now() - start < CAL_MOVE_TIMEOUT_MS) {
    const m = await calGetModuleState(module);
    if (m.moving) sawMoving = true;
    if (!m.moving && sawMoving) return { m, timedOut: false, sawMoving: true };
    await sleep(CAL_POLL_MS);
  }
  const m = await calGetModuleState(module);
  return { m, timedOut: !sawMoving, sawMoving };
}

async function calMoveToBlank() {
  const reportedChar = document.getElementById('calCurrentChar').value;
  const reportedIndex = CAL_FLAPS.indexOf(reportedChar);
  document.getElementById('calAskChar').style.display = 'none';
  document.getElementById('calConfirmBlank').style.display = 'block';
  const statusEl = document.getElementById('calBlankStatus');
  statusEl.className = 'cal-status';
  statusEl.textContent = 'Moving forward to blank...';

  const current = await calGetModuleState(calModule);
  const forward = (CAL_FLAPS.length - reportedIndex) % CAL_FLAPS.length;

  if (forward === 0) {
    // Already reported as blank -- skip issuing a move (same as
    // calibrate_single.py, which special-cases this rather than forcing an
    // unnecessary full rotation).
    statusEl.textContent = 'Already at blank (no move needed).';
    document.getElementById('calBlankConfirmRow').style.display = 'flex';
    return;
  }

  const target = (current.flapIndex + forward) % CAL_FLAPS.length;
  await fetch('/api/calibrate/move', { method: 'POST', body: JSON.stringify({ module: calModule, targetFlapIndex: target }) });
  const { m, timedOut } = await calWaitForIdle(calModule);

  if (timedOut) {
    statusEl.className = 'cal-status warn';
    statusEl.textContent = 'No movement detected within 20s -- module may be stalled. Take a look, then answer below anyway (or Cancel and power-cycle/retry).';
  } else {
    statusEl.textContent = 'Move finished. Firmware reports showing "' + (m.char === ' ' ? '(blank)' : m.char) + '".';
  }
  document.getElementById('calBlankConfirmRow').style.display = 'flex';
}

async function calBlankConfirmed(isBlank) {
  document.getElementById('calBlankConfirmRow').style.display = 'none';
  if (!isBlank) {
    await calAbort('The move-to-blank step landed somewhere wrong (likely a silent stall). Not safe to proceed -- SET_OFFSET would lock in the wrong position. Power-cycle if this keeps happening, then try again.');
    return;
  }

  document.getElementById('calBlankStatus').textContent = 'Blank confirmed. Locking in this position (SET_OFFSET)...';
  await fetch('/api/calibrate/setOffset', { method: 'POST', body: JSON.stringify({ module: calModule }) });

  document.getElementById('calConfirmA').style.display = 'block';
  const statusEl = document.getElementById('calAStatus');
  statusEl.className = 'cal-status';
  statusEl.textContent = "Commanding 'A' to verify...";

  await fetch('/api/calibrate/move', { method: 'POST', body: JSON.stringify({ module: calModule, targetFlapIndex: 1 }) });
  const { m, timedOut } = await calWaitForIdle(calModule);

  if (timedOut) {
    statusEl.className = 'cal-status warn';
    statusEl.textContent = 'No movement detected within 20s. Take a look, then answer below anyway (or Cancel).';
  } else {
    statusEl.textContent = 'Move finished. Firmware reports showing "' + m.char + '".';
  }
  document.getElementById('calAConfirmRow').style.display = 'flex';
}

async function calAConfirmed(isA) {
  document.getElementById('calAConfirmRow').style.display = 'none';
  if (!isA) {
    await calAbort("Verify failed -- module didn't show 'A'. Not saving. Power-cycle and try again.");
    return;
  }

  document.getElementById('calConfirmA').style.display = 'none';
  document.getElementById('calSaving').style.display = 'block';
  const statusEl = document.getElementById('calSaveStatus');
  statusEl.textContent = 'Returning to blank...';

  await fetch('/api/calibrate/move', { method: 'POST', body: JSON.stringify({ module: calModule, targetFlapIndex: 0 }) });
  await calWaitForIdle(calModule);

  statusEl.textContent = 'Saving calibration...';
  await fetch('/api/calibrate/save', { method: 'POST' });

  document.getElementById('calSaving').style.display = 'none';
  document.getElementById('calResult').style.display = 'block';
  const resultEl = document.getElementById('calResultStatus');
  resultEl.className = 'cal-status ok';
  resultEl.textContent = 'Module ' + calModule + ' calibrated and saved! Power-cycle to verify it lands on blank automatically.';
}

async function calAbort(message) {
  await fetch('/api/calibrate/cancel', { method: 'POST', body: JSON.stringify({ module: calModule }) });
  ['calAskChar', 'calConfirmBlank', 'calConfirmA', 'calSaving'].forEach(id => document.getElementById(id).style.display = 'none');
  document.getElementById('calResult').style.display = 'block';
  const resultEl = document.getElementById('calResultStatus');
  resultEl.className = 'cal-status err';
  resultEl.textContent = message;
}

async function calCancel() {
  await calAbort('Calibration cancelled.');
}

function calReset() {
  calModule = null;
  document.getElementById('calWizard').style.display = 'none';
  document.getElementById('calIdle').style.display = 'block';
}

// ---- Offset nudge (advanced) ----
// For a module that's already roughly calibrated but consistently
// under/over-shoots every commanded move by the same amount (e.g. module 3:
// always exactly 1 flap short, regardless of distance or destination). Shifts
// the saved offset by a small, deliberate amount via the existing
// IncreaseOffset mechanism (10 tenths = 1 flap) rather than a full
// recalibration. Reuses calWaitForIdle/calGetModuleState from the wizard above.
let nudgeModule = null;

function populateNudgeSelector() {
  const sel = document.getElementById('nudgeModuleSelect');
  if (sel.options.length !== calNumModules) {
    sel.innerHTML = '';
    for (let i = 0; i < calNumModules; i++) {
      const opt = document.createElement('option');
      opt.value = i;
      opt.textContent = 'Module ' + i;
      sel.appendChild(opt);
    }
  }
}

async function nudgeApply() {
  const module = parseInt(document.getElementById('nudgeModuleSelect').value);
  const tenths = parseInt(document.getElementById('nudgeTenths').value) || 10;

  if (nudgeModule === null) {
    const startRes = await fetch('/api/calibrate/start', { method: 'POST', body: JSON.stringify({ module }) });
    if (!startRes.ok) {
      const body = await startRes.json();
      alert(body.error || 'Could not start');
      return;
    }
    nudgeModule = module;
  }

  const res = await fetch('/api/calibrate/nudgeOffset', { method: 'POST', body: JSON.stringify({ module: nudgeModule, tenths }) });
  if (!res.ok) {
    const body = await res.json();
    alert(body.error || 'Nudge failed');
    return;
  }

  document.getElementById('nudgeIdle').style.display = 'none';
  document.getElementById('nudgeActive').style.display = 'block';
  document.getElementById('nudgeLabel').textContent = 'Nudging module ' + nudgeModule + ' (applied ' + tenths + ' tenths of a flap).';
  document.getElementById('nudgeStatus').textContent = '';
  document.getElementById('nudgeConfirmRow').style.display = 'none';
}

async function nudgeAgain() {
  const tenths = parseInt(document.getElementById('nudgeTenths').value) || 10;
  await fetch('/api/calibrate/nudgeOffset', { method: 'POST', body: JSON.stringify({ module: nudgeModule, tenths }) });
  document.getElementById('nudgeLabel').textContent = 'Applied another ' + tenths + ' tenths to module ' + nudgeModule + '.';
  document.getElementById('nudgeConfirmRow').style.display = 'none';
}

async function nudgeTestBlank() {
  const statusEl = document.getElementById('nudgeStatus');
  statusEl.className = 'cal-status';
  statusEl.textContent = 'Moving to blank...';
  document.getElementById('nudgeConfirmRow').style.display = 'none';

  await fetch('/api/calibrate/move', { method: 'POST', body: JSON.stringify({ module: nudgeModule, targetFlapIndex: 0 }) });
  const { m, timedOut } = await calWaitForIdle(nudgeModule);

  if (timedOut) {
    statusEl.className = 'cal-status warn';
    statusEl.textContent = 'No movement detected within 20s -- take a look anyway.';
  } else {
    statusEl.textContent = 'Firmware reports showing "' + (m.char === ' ' ? '(blank)' : m.char) + '".';
  }
  document.getElementById('nudgeConfirmRow').style.display = 'flex';
}

async function nudgeConfirmed(isBlank) {
  document.getElementById('nudgeConfirmRow').style.display = 'none';
  const statusEl = document.getElementById('nudgeStatus');
  if (isBlank) {
    await fetch('/api/calibrate/save', { method: 'POST' });
    statusEl.className = 'cal-status ok';
    statusEl.textContent = 'Saved. Module ' + nudgeModule + '\'s offset updated -- send a real message or let automation update to confirm future moves land correctly.';
    document.getElementById('nudgeActive').style.display = 'none';
    document.getElementById('nudgeIdle').style.display = 'block';
    nudgeModule = null;
  } else {
    statusEl.className = 'cal-status warn';
    statusEl.textContent = 'Not blank -- apply another nudge (try a smaller amount, e.g. 1-3 tenths, in either the same or a follow-up call) and check again, or Cancel to abandon without saving.';
  }
}

async function nudgeCancel() {
  if (nudgeModule !== null) {
    await fetch('/api/calibrate/cancel', { method: 'POST', body: JSON.stringify({ module: nudgeModule }) });
  }
  nudgeModule = null;
  document.getElementById('nudgeActive').style.display = 'none';
  document.getElementById('nudgeIdle').style.display = 'block';
}

// ---- Software reset ----
// Actually reboots the ESP32 (ESP.restart()) -- the real software equivalent
// of unplugging/replugging. Clears all firmware state, which is what's
// needed before a fresh calibration attempt (see calibrate_single.py notes
// on why a reconnect alone isn't enough).
async function restartDisplay() {
  if (!confirm('Restart the display? WiFi will be briefly unreachable, and any in-progress calibration will be lost.')) {
    return;
  }
  try {
    await fetch('/api/reset', { method: 'POST' });
  } catch (e) {
    // Expected -- the device may drop the connection before/while responding.
  }
  document.getElementById('statusLine').textContent = 'Restarting...';
  // The existing setInterval(refreshState, 3000) below will keep polling and
  // recover automatically once the device reboots and reconnects to WiFi.
}

refreshState();
loadPresets();
loadAutomation();
refreshTimer();
populateCalSelectors();
populateNudgeSelector();
setInterval(refreshState, 3000);
setInterval(refreshTimer, 3000);
</script>
</body>
</html>
)HTMLPAGE";
