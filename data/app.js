let currentFile = "";
let statusPollTimer = null;
let mouseFlushTimer = null;
let screenshotRefreshTimer = null;
let screenshotObjectUrl = "";
let activeTab = "editor";

const KEY = {
  CTRL: 128,
  SHIFT: 129,
  ALT: 130,
  GUI: 131,
  ENTER: 176,
  ESC: 177,
  BACKSPACE: 178,
  TAB: 179,
  CAPS: 193,
  F1: 194,
  F2: 195,
  F3: 196,
  F4: 197,
  F5: 198,
  F6: 199,
  F7: 200,
  F8: 201,
  F9: 202,
  F10: 203,
  F11: 204,
  F12: 205,
  INSERT: 209,
  HOME: 210,
  PAGE_UP: 211,
  DELETE: 212,
  END: 213,
  PAGE_DOWN: 214,
  RIGHT: 215,
  LEFT: 216,
  DOWN: 217,
  UP: 218,
};

const keyNameMap = {
  backspace: KEY.BACKSPACE,
  delete: KEY.DELETE,
  del: KEY.DELETE,
  left: KEY.LEFT,
  right: KEY.RIGHT,
  up: KEY.UP,
  down: KEY.DOWN,
  enter: KEY.ENTER,
  return: KEY.ENTER,
  tab: KEY.TAB,
  esc: KEY.ESC,
  escape: KEY.ESC,
  insert: KEY.INSERT,
  home: KEY.HOME,
  end: KEY.END,
  pageup: KEY.PAGE_UP,
  pgup: KEY.PAGE_UP,
  pagedown: KEY.PAGE_DOWN,
  pgdn: KEY.PAGE_DOWN,
  space: 32,
  spacebar: 32,
  caps: KEY.CAPS,
  capslock: KEY.CAPS,
};

const tuningHelp = {
  "typing-delay": {
    title: "Typing Delay",
    body: "Delay between each typed character. Increase this if characters are missing on slower target PCs. Decrease this to type faster.",
    best: "Best start: 2-6 ms. Use 5 ms for maximum reliability.",
  },
  "burst-chars": {
    title: "Burst Characters",
    body: "Number of characters sent in one burst before a short pause. Higher value increases speed but can overflow input buffers on some systems.",
    best: "Best start: 20-30. Use 24 as a safe balanced value.",
  },
  "burst-pause": {
    title: "Burst Pause",
    body: "Pause after each burst of characters. Increase this first when seeing dropped characters. Decrease this for more throughput.",
    best: "Best start: 8-14 ms. Use 10 ms for balanced speed and stability.",
  },
  "line-delay": {
    title: "Newline Pause",
    body: "Extra delay after Enter/newline. Helpful for command shells, editors, and form fields that need time to process each line.",
    best: "Best start: 30-60 ms. Use 40 ms for general use.",
  },
};

const keyboardLayout = [
  [
    { label: "Esc", code: KEY.ESC, w: 1.3 },
    { label: "`", code: "`".charCodeAt(0) },
    { label: "1", code: "1".charCodeAt(0) },
    { label: "2", code: "2".charCodeAt(0) },
    { label: "3", code: "3".charCodeAt(0) },
    { label: "4", code: "4".charCodeAt(0) },
    { label: "5", code: "5".charCodeAt(0) },
    { label: "6", code: "6".charCodeAt(0) },
    { label: "7", code: "7".charCodeAt(0) },
    { label: "8", code: "8".charCodeAt(0) },
    { label: "9", code: "9".charCodeAt(0) },
    { label: "0", code: "0".charCodeAt(0) },
    { label: "-", code: "-".charCodeAt(0) },
    { label: "=", code: "=".charCodeAt(0) },
    { label: "Backspace", code: KEY.BACKSPACE, w: 2.3 },
    { label: "Ins", code: KEY.INSERT, w: 1.2 },
    { label: "Home", code: KEY.HOME, w: 1.2 },
    { label: "PgUp", code: KEY.PAGE_UP, w: 1.3 },
  ],
  [
    { label: "Tab", code: KEY.TAB, w: 1.7 },
    { label: "Q", code: "q".charCodeAt(0) },
    { label: "W", code: "w".charCodeAt(0) },
    { label: "E", code: "e".charCodeAt(0) },
    { label: "R", code: "r".charCodeAt(0) },
    { label: "T", code: "t".charCodeAt(0) },
    { label: "Y", code: "y".charCodeAt(0) },
    { label: "U", code: "u".charCodeAt(0) },
    { label: "I", code: "i".charCodeAt(0) },
    { label: "O", code: "o".charCodeAt(0) },
    { label: "P", code: "p".charCodeAt(0) },
    { label: "[", code: "[".charCodeAt(0) },
    { label: "]", code: "]".charCodeAt(0) },
    { label: "\\", code: "\\".charCodeAt(0), w: 1.6 },
    { label: "Del", code: KEY.DELETE, w: 1.2 },
    { label: "End", code: KEY.END, w: 1.2 },
    { label: "PgDn", code: KEY.PAGE_DOWN, w: 1.3 },
  ],
  [
    { label: "Caps", code: KEY.CAPS, w: 2.1 },
    { label: "A", code: "a".charCodeAt(0) },
    { label: "S", code: "s".charCodeAt(0) },
    { label: "D", code: "d".charCodeAt(0) },
    { label: "F", code: "f".charCodeAt(0) },
    { label: "G", code: "g".charCodeAt(0) },
    { label: "H", code: "h".charCodeAt(0) },
    { label: "J", code: "j".charCodeAt(0) },
    { label: "K", code: "k".charCodeAt(0) },
    { label: "L", code: "l".charCodeAt(0) },
    { label: ";", code: ";".charCodeAt(0) },
    { label: "'", code: "'".charCodeAt(0) },
    { label: "Enter", code: KEY.ENTER, w: 2.4 },
  ],
  [
    { label: "Shift", code: KEY.SHIFT, w: 2.6 },
    { label: "Z", code: "z".charCodeAt(0) },
    { label: "X", code: "x".charCodeAt(0) },
    { label: "C", code: "c".charCodeAt(0) },
    { label: "V", code: "v".charCodeAt(0) },
    { label: "B", code: "b".charCodeAt(0) },
    { label: "N", code: "n".charCodeAt(0) },
    { label: "M", code: "m".charCodeAt(0) },
    { label: ",", code: ",".charCodeAt(0) },
    { label: ".", code: ".".charCodeAt(0) },
    { label: "/", code: "/".charCodeAt(0) },
    { label: "Shift", code: KEY.SHIFT, w: 2.6 },
    { label: "Up", code: KEY.UP, w: 1.3 },
  ],
  [
    { label: "Ctrl", code: KEY.CTRL, w: 1.6 },
    { label: "Win", code: KEY.GUI, w: 1.4 },
    { label: "Alt", code: KEY.ALT, w: 1.4 },
    { label: "Space", code: 32, w: 6.6 },
    { label: "Alt", code: KEY.ALT, w: 1.4 },
    { label: "Win", code: KEY.GUI, w: 1.4 },
    { label: "Ctrl", code: KEY.CTRL, w: 1.6 },
    { label: "Left", code: KEY.LEFT, w: 1.3 },
    { label: "Down", code: KEY.DOWN, w: 1.3 },
    { label: "Right", code: KEY.RIGHT, w: 1.3 },
  ],
];

const quickActionStorageKey = "esp32_custom_quick_actions_v3";
const keyboardPrefsStorageKey = "esp32_keyboard_prefs_v1";
const preferenceBundleVersion = 1;
const actionFileHeader = "# ESP32-HID-ACTION-V1";

const defaultKeyboardPrefs = {
  tapHoldMs: 35,
  comboHoldMs: 45,
  autoReleaseOnTabSwitch: true,
};

const usbVendorPresets = {
  espressif: {
    vendorName: "Espressif",
    vid: 0x303A,
    pid: 0x0002,
    productName: "ESP32-S3 HID Console",
  },
  arduino: {
    vendorName: "Arduino SA",
    vid: 0x2341,
    pid: 0x0036,
    productName: "Arduino Keyboard",
  },
  adafruit: {
    vendorName: "Adafruit",
    vid: 0x239A,
    pid: 0x810B,
    productName: "Adafruit HID Bridge",
  },
  logitech: {
    vendorName: "Logitech",
    vid: 0x046D,
    pid: 0xC31C,
    productName: "USB Keyboard",
  },
  microsoft: {
    vendorName: "Microsoft",
    vid: 0x045E,
    pid: 0x07F8,
    productName: "USB Input Device",
  },
};

let customQuickActions = [];
let keyboardPrefs = { ...defaultKeyboardPrefs };

const lockedModifiers = new Set();
const pointerPressedKeys = new Map();

const trackpadPointers = new Map();
const mouseAccum = {
  dx: 0,
  dy: 0,
  wheel: 0,
  pan: 0,
};

const recorder = {
  active: false,
  source: "ui",
  bridgeActive: false,
  lastEventAt: 0,
  events: [],
};

const bridgeMouseButtonMasks = [1, 2, 4, 8, 16];

const bridgeModifierMap = [
  { bit: 0x01, code: KEY.CTRL },
  { bit: 0x02, code: KEY.SHIFT },
  { bit: 0x04, code: KEY.ALT },
  { bit: 0x08, code: KEY.GUI },
  { bit: 0x10, code: KEY.CTRL },
  { bit: 0x20, code: KEY.SHIFT },
  { bit: 0x40, code: KEY.ALT },
  { bit: 0x80, code: KEY.GUI },
];

let leftMouseHeld = false;

function qs(id) {
  return document.getElementById(id);
}

function setText(id, text) {
  const el = qs(id);
  if (el) el.textContent = text;
}

function clampNumber(value, min, max) {
  const v = Number(value);
  if (!Number.isFinite(v)) return min;
  if (v < min) return min;
  if (v > max) return max;
  return v;
}

function getKvmMouseSmoothScale() {
  const slider = qs("kvm-mouse-smooth");
  const percent = slider ? clampNumber(slider.value, 25, 250) : 100;
  return Number(percent) / 100;
}

function formatHex16(value) {
  const v = clampNumber(value, 0, 0xFFFF);
  return `0x${Math.trunc(v).toString(16).toUpperCase().padStart(4, "0")}`;
}

function parseHexOrDecStrict(rawValue) {
  const raw = String(rawValue ?? "").trim();
  if (!raw) return null;

  const base = raw.toLowerCase().startsWith("0x") ? 16 : 10;
  const parsed = Number.parseInt(raw, base);
  if (!Number.isFinite(parsed)) return null;
  if (parsed < 0 || parsed > 0xFFFF) return null;
  return parsed;
}

function parseHexOrDec(rawValue, fallback) {
  const parsed = parseHexOrDecStrict(rawValue);
  return parsed == null ? fallback : parsed;
}

function downloadTextFile(filename, content, mimeType = "text/plain") {
  const blob = new Blob([content], { type: mimeType });
  const url = URL.createObjectURL(blob);

  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();

  URL.revokeObjectURL(url);
}

async function copyTextToClipboard(text, successLabel = "Copied.") {
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(text);
      setText("kvm-status", successLabel);
      return;
    }
  } catch (_) {}

  const ta = document.createElement("textarea");
  ta.value = text;
  ta.style.position = "fixed";
  ta.style.opacity = "0";
  document.body.appendChild(ta);
  ta.focus();
  ta.select();
  try {
    document.execCommand("copy");
    setText("kvm-status", successLabel);
  } catch (_) {
    setText("kvm-status", "Copy failed. Please copy manually.");
  } finally {
    document.body.removeChild(ta);
  }
}

async function api(path, options = {}) {
  const response = await fetch(path, options);

  if (response.status === 401) {
    window.location.href = "/login";
    throw new Error("unauthorized");
  }

  return response;
}

function updateRunBadge(busy) {
  const badge = qs("run-state");
  if (!badge) return;

  if (busy) {
    badge.className = "badge busy";
    badge.textContent = "Typing in progress";
  } else {
    badge.className = "badge ok";
    badge.textContent = "Ready";
  }
}

async function refreshStatus() {
  try {
    const res = await api("/api/status");
    if (!res.ok) return;
    const data = await res.json();
    updateRunBadge(Boolean(data.busy || data.queued));
  } catch (_) {}
}

async function loadFiles() {
  try {
    const res = await api("/api/list");
    if (!res.ok) {
      setText("editor-status", "Failed to load files.");
      return;
    }

    const files = await res.json();
    const list = qs("files-list");
    list.innerHTML = "";

    if (!files.length) {
      list.innerHTML = '<div class="small">No scripts yet.</div>';
      return;
    }

    files
      .sort((a, b) => a.name.localeCompare(b.name))
      .forEach((file) => {
        const btn = document.createElement("button");
        btn.className = "file-item" + (file.name === currentFile ? " active" : "");
        btn.textContent = file.name;
        btn.title = `${file.name} (${file.size} bytes)`;
        btn.addEventListener("click", () => loadFile(file.name));
        list.appendChild(btn);
      });
  } catch (_) {
    setText("editor-status", "Connection error while listing files.");
  }
}

async function loadFile(name) {
  try {
    const res = await api(`/api/load?name=${encodeURIComponent(name)}`);
    if (!res.ok) {
      setText("editor-status", "Failed to load script.");
      return;
    }

    const text = await res.text();
    currentFile = name;
    qs("code-area").value = text;
    setText("current-file", name);
    setText("editor-status", "Script loaded.");
    loadFiles();
  } catch (_) {
    setText("editor-status", "Network error while loading script.");
  }
}

function askNewFilename() {
  const name = prompt("New script name (example: demo.txt)", "demo.txt");
  if (!name) return "";
  const cleaned = name.trim();
  if (!/^[a-zA-Z0-9_.\- ]{1,64}$/.test(cleaned)) {
    alert("Allowed characters: letters, numbers, space, _, -, .");
    return "";
  }
  return cleaned;
}

function createNewFile() {
  const name = askNewFilename();
  if (!name) return;

  currentFile = name;
  qs("code-area").value = "GUI r\nDELAY 450\nSTRING notepad\nENTER\nDELAY 250\nSTRING Hello from ESP32-S3";
  setText("current-file", currentFile);
  setText("editor-status", "New file ready. Click Save.");
  loadFiles();
}

async function saveCurrentFile() {
  if (!currentFile) {
    createNewFile();
    if (!currentFile) return;
  }

  const content = qs("code-area").value;
  const body = new FormData();
  body.append("data", new Blob([content], { type: "text/plain" }), currentFile);

  try {
    const res = await api("/api/edit", { method: "POST", body });
    if (!res.ok) {
      setText("editor-status", "Save failed.");
      return;
    }

    setText("editor-status", "Saved.");
    loadFiles();
  } catch (_) {
    setText("editor-status", "Network error while saving.");
  }
}

async function deleteCurrentFile() {
  if (!currentFile) return;
  if (!confirm(`Delete ${currentFile}?`)) return;

  try {
    const res = await api(`/api/delete?name=${encodeURIComponent(currentFile)}`, {
      method: "DELETE",
    });
    if (!res.ok) {
      setText("editor-status", "Delete failed.");
      return;
    }

    currentFile = "";
    qs("code-area").value = "";
    setText("current-file", "No file selected");
    setText("editor-status", "Deleted.");
    loadFiles();
  } catch (_) {
    setText("editor-status", "Network error while deleting.");
  }
}

function downloadCurrentFile() {
  if (!currentFile) return;
  downloadTextFile(currentFile, qs("code-area").value, "text/plain");
}

async function runScript() {
  const code = qs("code-area").value;
  if (!code.trim()) return;

  setText("editor-status", "Queueing script...");
  try {
    const res = await api("/api/run", { method: "POST", body: code });
    if (!res.ok) {
      setText("editor-status", "Device busy or failed to queue.");
      return;
    }

    setText("editor-status", "Script queued.");
    refreshStatus();
  } catch (_) {
    setText("editor-status", "Network error while running script.");
  }
}

async function stopScript() {
  try {
    await api("/api/stop", { method: "POST" });
    setText("editor-status", "Stop signal sent.");
    setText("remote-status", "Stop signal sent.");
    refreshStatus();
  } catch (_) {
    setText("editor-status", "Failed to send stop signal.");
  }
}

async function injectLiveText() {
  const text = qs("live-text").value;
  if (!text) return;

  setText("remote-status", "Queueing text...");
  qs("inject-btn").disabled = true;

  try {
    const res = await api("/api/live_text", { method: "POST", body: text });
    if (!res.ok) {
      setText("remote-status", "Busy or failed to queue text.");
      return;
    }

    setText("remote-status", "Text queued.");
    qs("live-text").value = "";
    refreshStatus();
  } catch (_) {
    setText("remote-status", "Network error while queueing text.");
  } finally {
    qs("inject-btn").disabled = false;
  }
}

function parseKeyToken(token) {
  const lower = token.toLowerCase().trim();
  if (!lower) return null;

  if (Object.prototype.hasOwnProperty.call(keyNameMap, lower)) {
    return keyNameMap[lower];
  }

  const fnMatch = lower.match(/^f([1-9]|1[0-2])$/);
  if (fnMatch) {
    const fnNumber = Number(fnMatch[1]);
    return KEY.F1 + (fnNumber - 1);
  }

  if (lower.length === 1) {
    return lower.charCodeAt(0);
  }

  return null;
}

function modsToFlags(mods) {
  return (mods.ctrl ? 1 : 0)
    | (mods.alt ? 2 : 0)
    | (mods.shift ? 4 : 0)
    | (mods.gui ? 8 : 0);
}

function parseComboString(combo) {
  const rawParts = combo
    .toLowerCase()
    .split("+")
    .map((s) => s.trim())
    .filter(Boolean);

  if (!rawParts.length) return null;

  const mods = {
    ctrl: false,
    alt: false,
    shift: false,
    gui: false,
  };

  const lastToken = rawParts[rawParts.length - 1];
  for (let i = 0; i < rawParts.length - 1; i++) {
    const part = rawParts[i];
    if (part === "ctrl" || part === "control") mods.ctrl = true;
    else if (part === "alt" || part === "option") mods.alt = true;
    else if (part === "shift") mods.shift = true;
    else if (part === "gui" || part === "win" || part === "meta" || part === "cmd") mods.gui = true;
  }

  const code = parseKeyToken(lastToken);
  if (code == null) return null;

  return { mods, code, keyToken: lastToken };
}

function recordActionEvent(type, payload = {}) {
  if (!recorder.active || recorder.source !== "ui") return;

  const now = Date.now();
  const delay = recorder.lastEventAt > 0
    ? clampNumber(now - recorder.lastEventAt, 0, 60000)
    : 0;

  recorder.lastEventAt = now;
  recorder.events.push({
    delay: Math.trunc(delay),
    type,
    ...payload,
  });

  if (recorder.events.length > 8000) {
    recorder.active = false;
    setText("record-status", "Recorder stopped: max 8000 events reached.");
    return;
  }

  setText("record-status", `Recording... ${recorder.events.length} events`);
}

async function sendKeyTap(code, hold = keyboardPrefs.tapHoldMs, shouldRecord = true) {
  const safeHold = Math.trunc(clampNumber(hold, 10, 300));

  try {
    const res = await api("/api/live_key", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code, hold: safeHold }),
    });

    if (!res.ok) {
      setText("remote-status", "Key send failed (busy or invalid).");
      return false;
    }

    if (shouldRecord) {
      recordActionEvent("key_tap", { code, hold: safeHold });
    }
    setText("remote-status", `Key sent: ${code}`);
    return true;
  } catch (_) {
    setText("remote-status", "Network error while sending key.");
    return false;
  }
}

async function sendCombo(combo, hold = keyboardPrefs.comboHoldMs, shouldRecord = true) {
  const parsed = parseComboString(combo);
  if (!parsed) {
    setText("remote-status", "Invalid combo.");
    return false;
  }

  const safeHold = Math.trunc(clampNumber(hold, 10, 300));
  const payload = {
    ...parsed.mods,
    code: parsed.code,
    hold: safeHold,
  };

  try {
    const res = await api("/api/live_combo", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    if (!res.ok) {
      setText("remote-status", "Combo failed (busy or invalid).");
      return false;
    }

    if (shouldRecord) {
      recordActionEvent("combo", {
        flags: modsToFlags(parsed.mods),
        code: parsed.code,
        hold: safeHold,
      });
    }
    setText("remote-status", `Combo sent: ${combo}`);
    return true;
  } catch (_) {
    setText("remote-status", "Network error while sending combo.");
    return false;
  }
}

async function sendKeyboardEvent(action, code = null, hold = null, shouldRecord = true) {
  const payload = { action };
  if (code != null) payload.code = Number(code);
  if (hold != null) payload.hold = Math.trunc(clampNumber(hold, 10, 300));

  const res = await api("/api/kbd_event", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });

  if (res.ok && shouldRecord) {
    if (action === "down") {
      recordActionEvent("key_down", { code: Number(code) });
    } else if (action === "up") {
      recordActionEvent("key_up", { code: Number(code) });
    } else if (action === "release_all") {
      recordActionEvent("key_release_all", {});
    } else if (action === "tap") {
      recordActionEvent("key_tap", {
        code: Number(code),
        hold: payload.hold || keyboardPrefs.tapHoldMs,
      });
    }
  }

  return res.ok;
}

function loadCustomActions() {
  try {
    const raw = localStorage.getItem(quickActionStorageKey);
    if (!raw) {
      customQuickActions = [];
      return;
    }

    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) {
      customQuickActions = [];
      return;
    }

    customQuickActions = parsed
      .filter((item) => item && typeof item.label === "string" && typeof item.combo === "string")
      .map((item) => ({
        label: item.label.trim().slice(0, 40),
        combo: item.combo.trim().slice(0, 50),
      }))
      .filter((item) => item.label.length > 0 && item.combo.length > 0)
      .slice(0, 80);
  } catch (_) {
    customQuickActions = [];
  }
}

function saveCustomActions() {
  localStorage.setItem(quickActionStorageKey, JSON.stringify(customQuickActions));
}

function loadKeyboardPrefs() {
  try {
    const raw = localStorage.getItem(keyboardPrefsStorageKey);
    if (!raw) {
      keyboardPrefs = { ...defaultKeyboardPrefs };
      return;
    }

    const parsed = JSON.parse(raw);
    keyboardPrefs = {
      tapHoldMs: Math.trunc(clampNumber(parsed.tapHoldMs, 10, 180)),
      comboHoldMs: Math.trunc(clampNumber(parsed.comboHoldMs, 10, 220)),
      autoReleaseOnTabSwitch: Boolean(parsed.autoReleaseOnTabSwitch),
    };
  } catch (_) {
    keyboardPrefs = { ...defaultKeyboardPrefs };
  }
}

function saveKeyboardPrefs() {
  localStorage.setItem(keyboardPrefsStorageKey, JSON.stringify(keyboardPrefs));
}

function updateKeyboardPrefReadout(id, unit = "") {
  const input = qs(id);
  const out = qs(`${id}-value`);
  if (!input || !out) return;
  out.textContent = `${input.value}${unit}`;
}

function applyKeyboardPrefsToUI() {
  qs("kbd-pref-tap-hold").value = String(keyboardPrefs.tapHoldMs);
  qs("kbd-pref-combo-hold").value = String(keyboardPrefs.comboHoldMs);
  qs("kbd-pref-auto-release").checked = Boolean(keyboardPrefs.autoReleaseOnTabSwitch);
  updateKeyboardPrefReadout("kbd-pref-tap-hold", " ms");
  updateKeyboardPrefReadout("kbd-pref-combo-hold", " ms");
}

function bindKeyboardPreferenceControls() {
  const tap = qs("kbd-pref-tap-hold");
  const combo = qs("kbd-pref-combo-hold");
  const autoRelease = qs("kbd-pref-auto-release");

  tap.addEventListener("input", () => {
    keyboardPrefs.tapHoldMs = Math.trunc(clampNumber(tap.value, 10, 180));
    updateKeyboardPrefReadout("kbd-pref-tap-hold", " ms");
    saveKeyboardPrefs();
    setText("kbd-pref-status", "Keyboard preferences saved locally.");
  });

  combo.addEventListener("input", () => {
    keyboardPrefs.comboHoldMs = Math.trunc(clampNumber(combo.value, 10, 220));
    updateKeyboardPrefReadout("kbd-pref-combo-hold", " ms");
    saveKeyboardPrefs();
    setText("kbd-pref-status", "Keyboard preferences saved locally.");
  });

  autoRelease.addEventListener("change", () => {
    keyboardPrefs.autoReleaseOnTabSwitch = autoRelease.checked;
    saveKeyboardPrefs();
    setText("kbd-pref-status", "Keyboard preferences saved locally.");
  });
}

function exportPreferenceBundle() {
  const payload = {
    version: preferenceBundleVersion,
    exportedAt: new Date().toISOString(),
    customQuickActions,
    keyboardPrefs,
  };

  downloadTextFile("esp32-hid-preferences.json", JSON.stringify(payload, null, 2), "application/json");
  setText("remote-status", "Preferences exported.");
}

async function importPreferenceBundle(file) {
  const text = await file.text();
  const parsed = JSON.parse(text);

  if (!parsed || typeof parsed !== "object") {
    throw new Error("Invalid preferences file.");
  }

  if (Array.isArray(parsed.customQuickActions)) {
    customQuickActions = parsed.customQuickActions
      .filter((item) => item && typeof item.label === "string" && typeof item.combo === "string")
      .map((item) => ({
        label: item.label.trim().slice(0, 40),
        combo: item.combo.trim().slice(0, 50),
      }))
      .filter((item) => item.label.length > 0 && item.combo.length > 0)
      .slice(0, 80);
  }

  if (parsed.keyboardPrefs && typeof parsed.keyboardPrefs === "object") {
    keyboardPrefs = {
      tapHoldMs: Math.trunc(clampNumber(parsed.keyboardPrefs.tapHoldMs, 10, 180)),
      comboHoldMs: Math.trunc(clampNumber(parsed.keyboardPrefs.comboHoldMs, 10, 220)),
      autoReleaseOnTabSwitch: Boolean(parsed.keyboardPrefs.autoReleaseOnTabSwitch),
    };
  }

  saveCustomActions();
  saveKeyboardPrefs();
  renderCustomActions();
  applyKeyboardPrefsToUI();
  setText("remote-status", "Preferences imported.");
}

function renderCustomActions() {
  const container = qs("custom-actions-list");
  container.innerHTML = "";

  if (!customQuickActions.length) {
    container.innerHTML = '<div class="small">No custom actions yet.</div>';
    return;
  }

  customQuickActions.forEach((item, index) => {
    const wrap = document.createElement("div");
    wrap.className = "custom-action-item";

    const trigger = document.createElement("button");
    trigger.className = "key";
    trigger.textContent = item.label;
    trigger.title = item.combo;
    trigger.addEventListener("click", () => sendCombo(item.combo));

    const remove = document.createElement("button");
    remove.className = "remove-action-btn";
    remove.textContent = "x";
    remove.title = "Remove";
    remove.addEventListener("click", () => {
      customQuickActions.splice(index, 1);
      saveCustomActions();
      renderCustomActions();
    });

    wrap.appendChild(trigger);
    wrap.appendChild(remove);
    container.appendChild(wrap);
  });
}

function addCustomAction() {
  const label = qs("custom-action-label").value.trim();
  const combo = qs("custom-action-combo").value.trim();

  if (!label || !combo) {
    alert("Enter both label and shortcut.");
    return;
  }

  const parsed = parseComboString(combo);
  if (!parsed) {
    alert("Invalid shortcut key.");
    return;
  }

  customQuickActions.push({ label: label.slice(0, 40), combo: combo.slice(0, 50) });
  saveCustomActions();
  renderCustomActions();

  qs("custom-action-label").value = "";
  qs("custom-action-combo").value = "";
}

function renderKeyboard65() {
  const root = qs("keyboard-65");
  root.innerHTML = "";

  keyboardLayout.forEach((row) => {
    const rowEl = document.createElement("div");
    rowEl.className = "keyboard-row";

    row.forEach((keyDef) => {
      const keyEl = document.createElement("button");
      keyEl.className = "vk-key";
      keyEl.textContent = keyDef.label;
      keyEl.style.flexGrow = String(keyDef.w || 1);

      if (keyDef.code == null) {
        keyEl.disabled = true;
        keyEl.classList.add("disabled");
      } else {
        keyEl.dataset.code = String(keyDef.code);

        keyEl.addEventListener("pointerdown", (event) => {
          event.preventDefault();
          if (pointerPressedKeys.has(event.pointerId)) return;

          const code = Number(keyEl.dataset.code);
          sendKeyboardEvent("down", code, null, true)
            .then((ok) => {
              if (!ok) return;

              pointerPressedKeys.set(event.pointerId, { code, keyEl, label: keyDef.label });
              keyEl.classList.add("active");
              try {
                keyEl.setPointerCapture(event.pointerId);
              } catch (_) {}
              setText("keyboard-status", `Key down: ${keyDef.label}`);
            })
            .catch(() => {
              setText("keyboard-status", "Keyboard event failed.");
            });
        });

        const release = (pointerId) => {
          const active = pointerPressedKeys.get(pointerId);
          if (!active) return;

          pointerPressedKeys.delete(pointerId);
          active.keyEl.classList.remove("active");

          sendKeyboardEvent("up", active.code, null, true)
            .then(() => {
              setText("keyboard-status", `Key up: ${active.label}`);
            })
            .catch(() => {
              setText("keyboard-status", "Keyboard release failed.");
            });
        };

        keyEl.addEventListener("pointerup", (event) => release(event.pointerId));
        keyEl.addEventListener("pointercancel", (event) => release(event.pointerId));
        keyEl.addEventListener("lostpointercapture", (event) => release(event.pointerId));
      }

      rowEl.appendChild(keyEl);
    });

    root.appendChild(rowEl);
  });
}

function bindModifierLocks() {
  document.querySelectorAll(".mod-lock").forEach((button) => {
    button.addEventListener("click", async () => {
      const code = Number(button.dataset.modCode);
      if (!Number.isFinite(code)) return;

      if (lockedModifiers.has(code)) {
        try {
          await sendKeyboardEvent("up", code, null, true);
          lockedModifiers.delete(code);
          button.classList.remove("active");
          setText("keyboard-status", "Modifier unlocked.");
        } catch (_) {
          setText("keyboard-status", "Failed to unlock modifier.");
        }
      } else {
        try {
          await sendKeyboardEvent("down", code, null, true);
          lockedModifiers.add(code);
          button.classList.add("active");
          setText("keyboard-status", "Modifier locked.");
        } catch (_) {
          setText("keyboard-status", "Failed to lock modifier.");
        }
      }
    });
  });

  qs("release-all-keys-btn").addEventListener("click", async () => {
    try {
      await sendKeyboardEvent("release_all", null, null, true);
      lockedModifiers.clear();
      pointerPressedKeys.clear();
      document.querySelectorAll(".mod-lock").forEach((btn) => btn.classList.remove("active"));
      document.querySelectorAll(".vk-key.active").forEach((btn) => btn.classList.remove("active"));
      setText("keyboard-status", "All keys released.");
    } catch (_) {
      setText("keyboard-status", "Failed to release all keys.");
    }
  });
}

function buttonNameToMask(name) {
  const v = String(name || "").toLowerCase();
  if (v === "left") return 1;
  if (v === "right") return 2;
  if (v === "middle") return 4;
  if (v === "backward") return 8;
  if (v === "forward") return 16;
  return 1;
}

function mouseActionToCode(action) {
  if (action === "down") return 1;
  if (action === "up") return 2;
  return 0;
}

function startMouseFlushLoop() {
  if (mouseFlushTimer) return;

  mouseFlushTimer = setInterval(() => {
    const dx = Math.trunc(mouseAccum.dx);
    const dy = Math.trunc(mouseAccum.dy);
    const wheel = Math.trunc(mouseAccum.wheel);
    const pan = Math.trunc(mouseAccum.pan);

    if (dx !== 0 || dy !== 0) {
      mouseAccum.dx -= dx;
      mouseAccum.dy -= dy;

      api("/api/mouse_move", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ dx, dy }),
      })
        .then((res) => {
          if (res.ok) recordActionEvent("mouse_move", { dx, dy });
        })
        .catch(() => {});
    }

    if (wheel !== 0 || pan !== 0) {
      mouseAccum.wheel -= wheel;
      mouseAccum.pan -= pan;

      api("/api/mouse_scroll", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ wheel, pan }),
      })
        .then((res) => {
          if (res.ok) recordActionEvent("mouse_scroll", { wheel, pan });
        })
        .catch(() => {});
    }
  }, 28);
}

function bindTrackpad() {
  const trackpad = qs("trackpad");

  trackpad.addEventListener("pointerdown", (event) => {
    trackpad.setPointerCapture(event.pointerId);
    trackpadPointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    setText("mouse-status", "Trackpad active.");
  });

  trackpad.addEventListener("pointermove", (event) => {
    if (!trackpadPointers.has(event.pointerId)) return;

    const prev = trackpadPointers.get(event.pointerId);
    const dx = event.clientX - prev.x;
    const dy = event.clientY - prev.y;
    const smoothScale = getKvmMouseSmoothScale();

    trackpadPointers.set(event.pointerId, { x: event.clientX, y: event.clientY });

    if (trackpadPointers.size >= 2) {
      mouseAccum.wheel += (-dy * 0.2 * smoothScale);
      setText("mouse-status", "Gesture scroll.");
    } else {
      mouseAccum.dx += (dx * 1.45 * smoothScale);
      mouseAccum.dy += (dy * 1.45 * smoothScale);
      setText("mouse-status", "Pointer move.");
    }
  });

  const endPointer = (event) => {
    trackpadPointers.delete(event.pointerId);
    if (trackpadPointers.size === 0) {
      setText("mouse-status", "Mouse idle.");
    }
  };

  trackpad.addEventListener("pointerup", endPointer);
  trackpad.addEventListener("pointercancel", endPointer);

  qs("mouse-left-btn").addEventListener("click", async () => {
    try {
      const res = await api("/api/mouse_button", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ button: "left", action: "click" }),
      });
      if (!res.ok) throw new Error();

      recordActionEvent("mouse_button", { button: 1, action: 0 });
      setText("mouse-status", "Left click sent.");
    } catch (_) {
      setText("mouse-status", "Left click failed.");
    }
  });

  qs("mouse-right-btn").addEventListener("click", async () => {
    try {
      const res = await api("/api/mouse_button", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ button: "right", action: "click" }),
      });
      if (!res.ok) throw new Error();

      recordActionEvent("mouse_button", { button: 2, action: 0 });
      setText("mouse-status", "Right click sent.");
    } catch (_) {
      setText("mouse-status", "Right click failed.");
    }
  });

  qs("mouse-left-hold-btn").addEventListener("click", async () => {
    try {
      if (!leftMouseHeld) {
        const res = await api("/api/mouse_button", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ button: "left", action: "down" }),
        });
        if (!res.ok) throw new Error();

        leftMouseHeld = true;
        qs("mouse-left-hold-btn").textContent = "Release Left";
        recordActionEvent("mouse_button", { button: 1, action: 1 });
        setText("mouse-status", "Left button held.");
      } else {
        const res = await api("/api/mouse_button", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ button: "left", action: "up" }),
        });
        if (!res.ok) throw new Error();

        leftMouseHeld = false;
        qs("mouse-left-hold-btn").textContent = "Hold Left";
        recordActionEvent("mouse_button", { button: 1, action: 2 });
        setText("mouse-status", "Left button released.");
      }
    } catch (_) {
      setText("mouse-status", "Mouse hold action failed.");
    }
  });

  qs("mouse-release-btn").addEventListener("click", async () => {
    try {
      await api("/api/mouse_button", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ button: "left", action: "up" }),
      });
      await api("/api/mouse_button", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ button: "right", action: "up" }),
      });
      leftMouseHeld = false;
      qs("mouse-left-hold-btn").textContent = "Hold Left";
      recordActionEvent("mouse_button", { button: 1, action: 2 });
      recordActionEvent("mouse_button", { button: 2, action: 2 });
      setText("mouse-status", "Buttons released.");
    } catch (_) {
      setText("mouse-status", "Failed to release buttons.");
    }
  });
}

function updateSliderReadout(id, unit = "") {
  const input = qs(id);
  const out = qs(`${id}-value`);
  if (!input || !out) return;
  out.textContent = `${input.value}${unit}`;
}

function bindSliderReadout(id, unit = "") {
  const input = qs(id);
  if (!input) return;

  input.addEventListener("input", () => updateSliderReadout(id, unit));
  updateSliderReadout(id, unit);
}

function bindHelpPanel() {
  const panel = qs("help-panel");
  const title = qs("help-title");
  const body = qs("help-body");
  const best = qs("help-best");

  document.querySelectorAll(".help-icon").forEach((icon) => {
    icon.addEventListener("click", () => {
      const key = icon.dataset.helpKey;
      const info = tuningHelp[key];
      if (!info) return;

      title.textContent = info.title;
      body.textContent = info.body;
      best.textContent = info.best;
      panel.classList.remove("hidden");
    });
  });

  qs("help-close").addEventListener("click", () => panel.classList.add("hidden"));
}

function generateRandomToken(hexChars = 48) {
  const byteCount = Math.max(8, Math.floor(hexChars / 2));
  const bytes = new Uint8Array(byteCount);

  if (window.crypto && window.crypto.getRandomValues) {
    window.crypto.getRandomValues(bytes);
  } else {
    for (let i = 0; i < byteCount; i++) {
      bytes[i] = Math.floor(Math.random() * 256);
    }
  }

  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

function applyVendorPreset(presetKey) {
  if (presetKey === "custom") return;

  const preset = usbVendorPresets[presetKey];
  if (!preset) return;

  qs("usb-vendor-name").value = preset.vendorName;
  qs("usb-vendor-id").value = formatHex16(preset.vid);
  qs("usb-product-id").value = formatHex16(preset.pid);
  qs("usb-product-name").value = preset.productName;
}

function updateVendorPresetSelectionFromFields() {
  const vid = parseHexOrDecStrict(qs("usb-vendor-id").value);
  const vendorName = qs("usb-vendor-name").value.trim().toLowerCase();
  const productId = parseHexOrDecStrict(qs("usb-product-id").value);

  let matched = "custom";
  Object.entries(usbVendorPresets).forEach(([key, preset]) => {
    if (
      matched === "custom"
      && preset.vid === vid
      && preset.vendorName.toLowerCase() === vendorName
      && preset.pid === productId
    ) {
      matched = key;
    }
  });

  qs("usb-vendor-preset").value = matched;
}

function shellQuote(value) {
  const escaped = String(value || "").replace(/"/g, '\\"');
  return `"${escaped}"`;
}

function buildHostCommand(deviceIp, port, options = {}) {
  const host = deviceIp || "192.168.4.1";
  const noPreview = options.noPreview !== false;
  const blockLocalInput = Boolean(options.blockLocalInput);

  const base = [
    "python",
    "server/server.py",
    "--host", shellQuote(host),
    "--port", String(port || 4210),
    "--toggle-key", "f8",
  ];

  if (noPreview) {
    base.push("--no-preview");
  } else {
    base.push("--preview-port", "9876");
  }

  if (blockLocalInput) {
    base.push("--block-local-input");
  }

  return base.join(" ");
}

function updateKvmHelperPanel(statusData = {}) {
  const ip = statusData.device_ip || "192.168.4.1";
  const port = Number(statusData.port || qs("kvm-port")?.value || 4210);

  const cmdMain = buildHostCommand(ip, port, {
    noPreview: true,
    blockLocalInput: false,
  });
  const cmdNoPreview = buildHostCommand(ip, port, {
    noPreview: true,
    blockLocalInput: true,
  });
  const targetCaptureCommand = "python server/target_screenshot_server.py --port 9988";

  const mainEl = qs("kvm-cmd-main");
  const lowEl = qs("kvm-cmd-no-preview");
  const targetCaptureEl = qs("target-capture-cmd");
  if (mainEl) mainEl.value = cmdMain;
  if (lowEl) lowEl.value = cmdNoPreview;
  if (targetCaptureEl) targetCaptureEl.value = targetCaptureCommand;

  const screenshotUrlInput = qs("screenshot-url");
  if (screenshotUrlInput && !screenshotUrlInput.value.trim()) {
    screenshotUrlInput.value = "http://192.168.1.55:9988/screenshot.bmp";
  }
}

function updateKvmLinkIndicator(statusData = {}) {
  const dot = qs("kvm-link-dot");
  const stateEl = qs("kvm-link-state");
  const ageEl = qs("kvm-link-age");
  if (!dot || !stateEl || !ageEl) return;

  dot.classList.remove("connected", "warning", "offline");

  const linkState = String(statusData.link_state || "unknown");
  const age = Number(statusData.packet_age_ms);
  const hasAge = Number.isFinite(age) && age >= 0 && age < 0xFFFFFF00;

  let label = "Status: unknown";
  if (linkState === "connected") {
    dot.classList.add("connected");
    label = "Status: connected";
  } else if (linkState === "bind-failed") {
    dot.classList.add("offline");
    label = "Status: bind failed";
  } else if (linkState === "waiting" || linkState === "stale") {
    dot.classList.add("warning");
    label = linkState === "waiting" ? "Status: waiting for packets" : "Status: stale";
  } else if (linkState === "disabled") {
    label = "Status: disabled";
  } else {
    dot.classList.add("offline");
    label = "Status: offline";
  }

  stateEl.textContent = label;
  ageEl.textContent = hasAge ? `Last packet age: ${age} ms` : "Last packet age: -";
}

function sanitizeActionFilename(name) {
  const cleaned = String(name || "").trim();
  if (!/^[a-zA-Z0-9_.\- ]{1,64}$/.test(cleaned)) return "";
  return cleaned;
}

function getRecordSource() {
  const raw = qs("record-source")?.value;
  return raw === "kvm_bridge" ? "kvm_bridge" : "ui";
}

function modifierCodesFromMask(mask) {
  const codeSet = new Set();
  bridgeModifierMap.forEach((entry) => {
    if (mask & entry.bit) {
      codeSet.add(entry.code);
    }
  });
  return Array.from(codeSet);
}

function normalizedBridgeKeySet(keys) {
  const out = new Set();
  if (!Array.isArray(keys)) return out;

  keys.forEach((key) => {
    const code = Math.trunc(clampNumber(key, 0, 255));
    if (code > 0) out.add(code);
  });

  return out;
}

function convertBridgeEventsToRecorderEvents(bridgeEvents = []) {
  const out = [];
  let previousDt = 0;
  let previousButtons = 0;
  let previousModifiers = 0;
  let previousKeys = new Set();

  bridgeEvents.forEach((rawEvent) => {
    const event = rawEvent && typeof rawEvent === "object" ? rawEvent : {};
    const dt = Math.trunc(clampNumber(event.dt, 0, 3_600_000));
    const baseDelay = Math.max(0, dt - previousDt);
    previousDt = dt;

    let firstAction = true;
    const pushAction = (action) => {
      out.push({
        delay: firstAction ? baseDelay : 0,
        ...action,
      });
      firstAction = false;
    };

    if (event.type === "mouse") {
      const dx = Math.trunc(clampNumber(event.dx, -4096, 4096));
      const dy = Math.trunc(clampNumber(event.dy, -4096, 4096));
      const wheel = Math.trunc(clampNumber(event.wheel, -127, 127));
      const pan = Math.trunc(clampNumber(event.pan, -127, 127));
      const buttons = Math.trunc(clampNumber(event.buttons, 0, 31));

      if (dx !== 0 || dy !== 0) {
        pushAction({ type: "mouse_move", dx, dy });
      }
      if (wheel !== 0 || pan !== 0) {
        pushAction({ type: "mouse_scroll", wheel, pan });
      }

      const changed = previousButtons ^ buttons;
      bridgeMouseButtonMasks.forEach((mask) => {
        if ((changed & mask) === 0) return;
        const action = (buttons & mask) ? 1 : 2;
        pushAction({ type: "mouse_button", button: mask, action });
      });

      previousButtons = buttons;
      return;
    }

    if (event.type === "keyboard") {
      const modifierMask = Math.trunc(clampNumber(event.modifiers, 0, 255));
      const currentModifierCodes = modifierCodesFromMask(modifierMask);
      const previousModifierCodes = modifierCodesFromMask(previousModifiers);

      previousModifierCodes.forEach((code) => {
        if (!currentModifierCodes.includes(code)) {
          pushAction({ type: "key_up", code });
        }
      });

      const currentKeys = normalizedBridgeKeySet(event.keys);
      previousKeys.forEach((code) => {
        if (!currentKeys.has(code)) {
          pushAction({ type: "key_up", code });
        }
      });

      currentModifierCodes.forEach((code) => {
        if (!previousModifierCodes.includes(code)) {
          pushAction({ type: "key_down", code });
        }
      });

      currentKeys.forEach((code) => {
        if (!previousKeys.has(code)) {
          pushAction({ type: "key_down", code });
        }
      });

      previousModifiers = modifierMask;
      previousKeys = currentKeys;
      return;
    }

    if (event.type === "consumer") {
      const usage = Math.trunc(clampNumber(event.usage, 0, 0xFFFF));
      pushAction({ type: "consumer", usage });
    }
  });

  if (previousButtons !== 0 || previousKeys.size > 0 || previousModifiers !== 0) {
    out.push({ delay: 0, type: "key_release_all" });
    bridgeMouseButtonMasks.forEach((mask) => {
      if (previousButtons & mask) {
        out.push({ delay: 0, type: "mouse_button", button: mask, action: 2 });
      }
    });
  }

  return out;
}

async function loadBridgeRecordStatus() {
  const el = qs("record-bridge-status");
  if (!el) return;

  try {
    const res = await api("/api/kvm_bridge_record_status");
    if (!res.ok) {
      el.textContent = "Bridge recorder status unavailable.";
      return;
    }

    const data = await res.json();
    const enabled = Boolean(data.enabled);
    const count = Math.trunc(clampNumber(data.count, 0, 1_000_000));
    const dropped = Math.trunc(clampNumber(data.dropped, 0, 1_000_000));
    const duration = Math.trunc(clampNumber(data.duration_ms, 0, 86_400_000));

    if (enabled) {
      el.textContent = `Bridge recording active: ${count} events (${dropped} dropped, ${duration} ms).`;
    } else {
      el.textContent = `Bridge recorder ready: ${count} events buffered (${dropped} dropped).`;
    }
  } catch (_) {
    el.textContent = "Bridge recorder status unavailable.";
  }
}

function encodeActionFile(events) {
  const lines = [
    actionFileHeader,
    "# delay_ms|event|params",
  ];

  events.forEach((event) => {
    const delay = Math.trunc(clampNumber(event.delay, 0, 60000));

    if (event.type === "key_tap") {
      lines.push(`${delay}|key_tap|${Math.trunc(clampNumber(event.code, 0, 255))}|${Math.trunc(clampNumber(event.hold, 10, 300))}`);
    } else if (event.type === "key_down") {
      lines.push(`${delay}|key_down|${Math.trunc(clampNumber(event.code, 0, 255))}`);
    } else if (event.type === "key_up") {
      lines.push(`${delay}|key_up|${Math.trunc(clampNumber(event.code, 0, 255))}`);
    } else if (event.type === "key_release_all") {
      lines.push(`${delay}|key_release_all`);
    } else if (event.type === "combo") {
      lines.push(`${delay}|combo|${Math.trunc(clampNumber(event.flags, 0, 15))}|${Math.trunc(clampNumber(event.code, 0, 255))}|${Math.trunc(clampNumber(event.hold, 10, 300))}`);
    } else if (event.type === "mouse_move") {
      lines.push(`${delay}|mouse_move|${Math.trunc(clampNumber(event.dx, -2048, 2048))}|${Math.trunc(clampNumber(event.dy, -2048, 2048))}`);
    } else if (event.type === "mouse_scroll") {
      lines.push(`${delay}|mouse_scroll|${Math.trunc(clampNumber(event.wheel, -127, 127))}|${Math.trunc(clampNumber(event.pan, -127, 127))}`);
    } else if (event.type === "mouse_button") {
      lines.push(`${delay}|mouse_button|${Math.trunc(clampNumber(event.button, 1, 31))}|${Math.trunc(clampNumber(event.action, 0, 2))}`);
    } else if (event.type === "consumer") {
      lines.push(`${delay}|consumer|${Math.trunc(clampNumber(event.usage, 0, 0xFFFF))}`);
    }
  });

  return `${lines.join("\n")}\n`;
}

async function startRecording() {
  const source = getRecordSource();
  recorder.source = source;
  recorder.lastEventAt = 0;
  recorder.events = [];
  recorder.active = false;
  recorder.bridgeActive = false;

  if (source === "ui") {
    recorder.active = true;
    setText("record-status", "UI recording started. Use Remote/Keyboard/Mouse controls now.");
    return;
  }

  try {
    const res = await api("/api/kvm_bridge_record_start", { method: "POST" });
    if (!res.ok) {
      setText("record-status", "Failed to start KVM bridge recording.");
      return;
    }

    recorder.bridgeActive = true;
    setText("record-status", "Bridge recording started. Generate traffic from host sender now.");
    await loadBridgeRecordStatus();
  } catch (_) {
    setText("record-status", "Network error while starting bridge recording.");
  }
}

async function stopRecording() {
  if (recorder.source !== "kvm_bridge") {
    recorder.active = false;
    if (recorder.events.length > 0) {
      setText("record-status", `Recording stopped. ${recorder.events.length} events captured.`);
    } else {
      setText("record-status", "Recording stopped with 0 events. Trigger UI input while recording.");
    }
    return;
  }

  if (!recorder.bridgeActive) {
    setText("record-status", "Bridge recorder is not running.");
    return;
  }

  try {
    const stopRes = await api("/api/kvm_bridge_record_stop", { method: "POST" });
    if (!stopRes.ok) {
      setText("record-status", "Failed to stop bridge recorder.");
      return;
    }

    const exportRes = await api("/api/kvm_bridge_record_export");
    if (!exportRes.ok) {
      setText("record-status", "Bridge recorder stopped, but export failed.");
      recorder.bridgeActive = false;
      return;
    }

    const data = await exportRes.json();
    const bridgeEvents = Array.isArray(data.events) ? data.events : [];
    recorder.events = convertBridgeEventsToRecorderEvents(bridgeEvents);
    recorder.bridgeActive = false;
    recorder.active = false;
    recorder.lastEventAt = 0;

    const dropped = Math.trunc(clampNumber(data.dropped, 0, 1_000_000));
    setText(
      "record-status",
      `Bridge recording imported: ${recorder.events.length} replay events (${bridgeEvents.length} raw packets, ${dropped} dropped).`,
    );
    await loadBridgeRecordStatus();
  } catch (_) {
    setText("record-status", "Network error while stopping bridge recorder.");
  }
}

async function clearRecording() {
  recorder.active = false;
  recorder.lastEventAt = 0;
  recorder.events = [];

  const shouldClearBridge = getRecordSource() === "kvm_bridge" || recorder.source === "kvm_bridge";
  recorder.bridgeActive = false;

  if (shouldClearBridge) {
    try {
      await api("/api/kvm_bridge_record_stop", { method: "POST" });
      await api("/api/kvm_bridge_record_clear", { method: "POST" });
      await loadBridgeRecordStatus();
    } catch (_) {
      setText("record-status", "Local recorder cleared, but bridge clear failed.");
      return;
    }
  }

  setText("record-status", "Recorder cleared.");
}

async function saveRecordingToDevice() {
  if (recorder.bridgeActive) {
    setText("record-status", "Stop bridge recording before saving.");
    return;
  }

  if (!recorder.events.length) {
    setText("record-status", "Nothing to save. Record actions first.");
    return;
  }

  const entered = qs("record-file-name").value.trim() || `action-${Date.now()}.txt`;
  const safeName = sanitizeActionFilename(entered);
  if (!safeName) {
    setText("record-status", "Invalid file name. Allowed: letters, numbers, space, _, -, .");
    return;
  }

  const content = encodeActionFile(recorder.events);
  try {
    const res = await api(`/api/action_file/save?name=${encodeURIComponent(safeName)}`, {
      method: "POST",
      headers: { "Content-Type": "text/plain" },
      body: content,
    });

    if (!res.ok) {
      setText("record-status", "Failed to save recording to ESP.");
      return;
    }

    qs("record-file-name").value = safeName;
    await loadActionFiles();
    setText("record-status", `Saved to ESP as ${safeName}.`);
  } catch (_) {
    setText("record-status", "Network error while saving recording.");
  }
}

function downloadRecordingFile() {
  if (recorder.bridgeActive) {
    setText("record-status", "Stop bridge recording before downloading.");
    return;
  }

  if (!recorder.events.length) {
    setText("record-status", "No recording available.");
    return;
  }

  const entered = qs("record-file-name").value.trim() || `action-${Date.now()}.txt`;
  const safeName = sanitizeActionFilename(entered) || `action-${Date.now()}.txt`;
  downloadTextFile(safeName, encodeActionFile(recorder.events), "text/plain");
  setText("record-status", "Recording downloaded.");
}

function selectedActionFileName() {
  const select = qs("action-files-select");
  if (!select || !select.value) return "";
  return sanitizeActionFilename(select.value);
}

async function loadActionFiles() {
  const select = qs("action-files-select");
  if (!select) return;

  try {
    const res = await api("/api/action_files");
    if (!res.ok) {
      setText("record-status", "Failed to list action files.");
      return;
    }

    const files = await res.json();
    select.innerHTML = "";

    if (!Array.isArray(files) || files.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "No action files";
      select.appendChild(option);
      return;
    }

    files
      .sort((a, b) => a.name.localeCompare(b.name))
      .forEach((file) => {
        const option = document.createElement("option");
        option.value = file.name;
        option.textContent = `${file.name} (${file.size} bytes)`;
        select.appendChild(option);
      });
  } catch (_) {
    setText("record-status", "Network error while listing action files.");
  }
}

async function runSelectedActionFile() {
  const name = selectedActionFileName();
  if (!name) {
    setText("record-status", "Select an action file first.");
    return;
  }

  try {
    const res = await api(`/api/action_file/run?name=${encodeURIComponent(name)}`, { method: "POST" });
    if (!res.ok) {
      setText("record-status", "Failed to queue action file.");
      return;
    }

    setText("record-status", `Queued action file: ${name}`);
    refreshStatus();
  } catch (_) {
    setText("record-status", "Network error while queueing action file.");
  }
}

async function deleteSelectedActionFile() {
  const name = selectedActionFileName();
  if (!name) {
    setText("record-status", "Select an action file first.");
    return;
  }

  if (!confirm(`Delete action file ${name}?`)) return;

  try {
    const res = await api(`/api/action_file/delete?name=${encodeURIComponent(name)}`, {
      method: "DELETE",
    });
    if (!res.ok) {
      setText("record-status", "Failed to delete action file.");
      return;
    }

    await loadActionFiles();
    setText("record-status", "Action file deleted.");
  } catch (_) {
    setText("record-status", "Network error while deleting action file.");
  }
}

async function importActionFileToDevice(file) {
  const safeName = sanitizeActionFilename(file?.name || "imported-action.txt");
  if (!safeName) {
    setText("record-status", "Invalid imported file name.");
    return;
  }

  const text = await file.text();
  if (!text || text.length < 8) {
    setText("record-status", "Imported file is empty.");
    return;
  }

  try {
    const res = await api(`/api/action_file/save?name=${encodeURIComponent(safeName)}`, {
      method: "POST",
      headers: { "Content-Type": "text/plain" },
      body: text,
    });

    if (!res.ok) {
      setText("record-status", "Failed to import file to ESP.");
      return;
    }

    qs("record-file-name").value = safeName;
    await loadActionFiles();
    setText("record-status", `Imported and saved as ${safeName}.`);
  } catch (_) {
    setText("record-status", "Network error while importing action file.");
  }
}

async function loadKvmStatus() {
  try {
    const res = await api("/api/kvm_status");
    if (!res.ok) {
      setText("kvm-status", "Failed to load KVM status.");
      return;
    }

    const d = await res.json();

    qs("kvm-enabled").checked = Boolean(d.enabled);
    qs("kvm-port").value = d.port ?? 4210;
    qs("kvm-allowed-ip").value = d.allowed_ip || "";

    setText("kvm-packets-rx", String(d.packets_rx ?? 0));
    setText("kvm-packets-dropped", String(d.packets_dropped ?? 0));
    setText("kvm-last-seq", d.last_sequence != null ? String(d.last_sequence) : "-");
    setText("kvm-last-source", d.last_source_ip || "-");
    updateKvmLinkIndicator(d);
    updateKvmHelperPanel(d);
    loadBridgeRecordStatus();

    const listening = d.enabled && d.bound;
    const ip = d.device_ip || "ESP32 IP";
    const configuredPort = Number(d.port || 4210);
    const boundPort = Number(d.bound_port || 0);
    const effectivePort = boundPort > 0 ? boundPort : configuredPort;
    if (d.connected) {
      const age = Number.isFinite(Number(d.packet_age_ms)) ? `, age ${d.packet_age_ms} ms` : "";
      setText("kvm-status", `Connected on UDP ${effectivePort} (${ip}${age})`);
    } else if (listening) {
      setText("kvm-status", `Listening on UDP ${effectivePort} (${ip}), waiting packets`);
    } else if (d.enabled && String(d.bind_error || "").length > 0) {
      setText("kvm-status", `KVM enabled but bind failed on UDP ${configuredPort} (${d.bind_error}).`);
    } else if (d.enabled) {
      setText("kvm-status", `KVM enabled but UDP ${configuredPort} not bound yet.`);
    } else {
      setText("kvm-status", "KVM listener disabled.");
    }
  } catch (_) {
    setText("kvm-status", "Network error while loading KVM status.");
  }
}

function getScreenshotRefreshIntervalMs() {
  return Math.trunc(clampNumber(qs("screenshot-interval-ms")?.value || 2000, 500, 10000));
}

function cleanupScreenshotObjectUrl() {
  if (screenshotObjectUrl) {
    URL.revokeObjectURL(screenshotObjectUrl);
    screenshotObjectUrl = "";
  }
}

async function loadScreenshotPreview() {
  const img = qs("screenshot-image");
  const urlInput = qs("screenshot-url");
  if (!img || !urlInput) return;

  const baseUrl = urlInput.value.trim();
  if (!baseUrl) {
    setText("screenshot-status", "Enter target screenshot URL first.");
    return;
  }

  const fetchUrl = baseUrl.includes("?") ? `${baseUrl}&_t=${Date.now()}` : `${baseUrl}?_t=${Date.now()}`;
  setText("screenshot-status", "Loading target screenshot...");

  try {
    const res = await fetch(fetchUrl, { cache: "no-store" });
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const blob = await res.blob();
    if (!blob || blob.size < 32) {
      throw new Error("empty-image");
    }

    cleanupScreenshotObjectUrl();
    screenshotObjectUrl = URL.createObjectURL(blob);
    img.src = screenshotObjectUrl;
    img.classList.add("visible");
    setText("screenshot-status", `Target screenshot updated (${blob.size} bytes).`);
  } catch (err) {
    setText("screenshot-status", `Target screenshot load failed: ${String(err?.message || err)}`);
  }
}

function stopScreenshotAutoRefresh() {
  if (screenshotRefreshTimer) {
    clearInterval(screenshotRefreshTimer);
    screenshotRefreshTimer = null;
  }
}

function syncScreenshotAutoRefresh() {
  const enabled = Boolean(qs("screenshot-auto-refresh")?.checked);
  stopScreenshotAutoRefresh();

  if (!enabled) return;

  const interval = getScreenshotRefreshIntervalMs();
  screenshotRefreshTimer = setInterval(() => {
    if (activeTab === "kvm") {
      loadScreenshotPreview();
    }
  }, interval);
}

async function saveKvmConfig() {
  const port = Math.trunc(clampNumber(qs("kvm-port").value, 1, 65535));
  qs("kvm-port").value = String(port);
  const payload = {
    enabled: qs("kvm-enabled").checked,
    port,
    allowed_ip: qs("kvm-allowed-ip").value.trim(),
  };

  try {
    const res = await api("/api/kvm_config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    if (!res.ok) {
      setText("kvm-status", "Failed to apply KVM config.");
      return;
    }

    let bindMessage = "KVM config saved.";
    try {
      const info = await res.json();
      if (payload.enabled && !info.bound) {
        const err = String(info.bind_error || "not-bound");
        bindMessage = `KVM saved, but UDP bind failed (${err}).`;
      } else if (payload.enabled && info.bound) {
        const effectivePort = Math.trunc(clampNumber(info.bound_port || port, 1, 65535));
        bindMessage = `KVM config saved and listening on UDP ${effectivePort}.`;
      }
    } catch (_) {}

    setText("kvm-status", bindMessage);
    await loadKvmStatus();
  } catch (_) {
    setText("kvm-status", "Network error while saving KVM config.");
  }
}

async function releaseAllHid() {
  try {
    const res = await api("/api/hid_release_all", { method: "POST" });
    if (!res.ok) {
      setText("kvm-status", "Failed to release HID state.");
      return;
    }

    lockedModifiers.clear();
    pointerPressedKeys.clear();
    document.querySelectorAll(".mod-lock").forEach((btn) => btn.classList.remove("active"));
    document.querySelectorAll(".vk-key.active").forEach((btn) => btn.classList.remove("active"));
    leftMouseHeld = false;
    qs("mouse-left-hold-btn").textContent = "Hold Left";
    setText("kvm-status", "All HID keys/buttons released.");
  } catch (_) {
    setText("kvm-status", "Network error while releasing HID state.");
  }
}

async function loadSettings() {
  try {
    const res = await api("/api/get_settings");
    if (!res.ok) {
      setText("settings-status", "Failed to load settings.");
      return;
    }

    const d = await res.json();
    qs("ap-ssid").value = d.ap_ssid || "";
    qs("ap-pass").value = d.ap_pass || "";
    qs("sta-ssid").value = d.sta_ssid || "";
    qs("sta-pass").value = d.sta_pass || "";
    qs("admin-user").value = d.admin_user || "admin";

    qs("typing-delay").value = d.delay ?? 6;
    qs("burst-chars").value = d.burst_chars ?? 24;
    qs("burst-pause").value = d.burst_pause ?? 10;
    qs("line-delay").value = d.line_delay ?? 40;
    qs("kvm-mouse-smooth").value = d.kvm_mouse_smooth ?? 100;
    qs("led-bright").value = d.bright ?? 50;

    qs("login-rate-limit").checked = d.login_rate_limit ?? true;
    qs("proxy-auth-enabled").checked = d.proxy_auth_enabled ?? false;
    qs("proxy-auth-token").value = d.proxy_auth_token || "";

    qs("usb-vendor-name").value = d.usb_vendor_name || "Espressif";
    qs("usb-product-name").value = d.usb_product_name || "ESP32-S3 HID Console";
    qs("usb-vendor-id").value = formatHex16(d.usb_vid ?? 0x303A);
    qs("usb-product-id").value = formatHex16(d.usb_pid ?? 0x0002);

    if (typeof d.kvm_enabled === "boolean") qs("kvm-enabled").checked = d.kvm_enabled;
    if (d.kvm_port != null) qs("kvm-port").value = d.kvm_port;
    if (typeof d.kvm_allowed_ip === "string") qs("kvm-allowed-ip").value = d.kvm_allowed_ip;

    updateSliderReadout("typing-delay", " ms");
    updateSliderReadout("burst-chars", "");
    updateSliderReadout("burst-pause", " ms");
    updateSliderReadout("line-delay", " ms");
    updateSliderReadout("kvm-mouse-smooth", "%");

    updateVendorPresetSelectionFromFields();
    setText("settings-status", "Settings loaded.");
  } catch (_) {
    setText("settings-status", "Network error while loading settings.");
  }
}

async function saveSettings() {
  const usbVid = parseHexOrDecStrict(qs("usb-vendor-id").value);
  const usbPid = parseHexOrDecStrict(qs("usb-product-id").value);

  if (usbVid == null || usbPid == null) {
    setText("settings-status", "USB VID/PID must be valid 16-bit hex or decimal values.");
    return;
  }

  const payload = {
    ap_ssid: qs("ap-ssid").value,
    ap_pass: qs("ap-pass").value,
    sta_ssid: qs("sta-ssid").value,
    sta_pass: qs("sta-pass").value,
    admin_user: qs("admin-user").value,

    delay: Number(qs("typing-delay").value),
    burst_chars: Number(qs("burst-chars").value),
    burst_pause: Number(qs("burst-pause").value),
    line_delay: Number(qs("line-delay").value),
    kvm_mouse_smooth: Number(qs("kvm-mouse-smooth").value),
    bright: Number(qs("led-bright").value),

    login_rate_limit: qs("login-rate-limit").checked,
    proxy_auth_enabled: qs("proxy-auth-enabled").checked,
    proxy_auth_token: qs("proxy-auth-token").value.trim(),

    usb_vendor_name: qs("usb-vendor-name").value.trim(),
    usb_product_name: qs("usb-product-name").value.trim(),
    usb_vid: usbVid,
    usb_pid: usbPid,

    kvm_enabled: qs("kvm-enabled").checked,
    kvm_port: Math.trunc(clampNumber(qs("kvm-port").value, 1, 65535)),
    kvm_allowed_ip: qs("kvm-allowed-ip").value.trim(),
  };

  const newPass = qs("admin-pass").value.trim();
  if (newPass.length > 0) {
    payload.admin_pass = newPass;
  }

  try {
    const res = await api("/api/save_settings", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    if (!res.ok) {
      setText("settings-status", "Failed to save settings.");
      return;
    }

    let restartNeeded = false;
    try {
      const responseData = await res.json();
      restartNeeded = Boolean(responseData.usb_restart_required);
    } catch (_) {}

    qs("admin-pass").value = "";
    if (restartNeeded) {
      setText("settings-status", "Settings saved. USB identity changes will apply after reboot.");
    } else {
      setText("settings-status", "Settings saved.");
    }

    updateVendorPresetSelectionFromFields();
    await loadKvmStatus();
  } catch (_) {
    setText("settings-status", "Network error while saving settings.");
  }
}

async function rebootDevice() {
  if (!confirm("Reboot ESP32 now?")) return;
  try {
    await api("/api/reboot", { method: "POST" });
    setText("settings-status", "Reboot command sent. Device will reconnect shortly.");
  } catch (_) {
    setText("settings-status", "Failed to send reboot command.");
  }
}

async function logout() {
  try {
    await api("/api/logout", { method: "POST" });
  } catch (_) {}
  window.location.href = "/login";
}

function setupTabs() {
  const buttons = Array.from(document.querySelectorAll(".tab-btn"));
  const views = Array.from(document.querySelectorAll(".view"));

  async function activate(tab) {
    if (
      activeTab === "keyboard"
      && tab !== "keyboard"
      && keyboardPrefs.autoReleaseOnTabSwitch
    ) {
      try {
        await sendKeyboardEvent("release_all", null, null, false);
      } catch (_) {}
      lockedModifiers.clear();
      pointerPressedKeys.clear();
      document.querySelectorAll(".mod-lock").forEach((btn) => btn.classList.remove("active"));
      document.querySelectorAll(".vk-key.active").forEach((btn) => btn.classList.remove("active"));
    }

    activeTab = tab;
    buttons.forEach((b) => b.classList.toggle("active", b.dataset.tab === tab));
    views.forEach((v) => v.classList.toggle("active", v.id === `view-${tab}`));

    if (tab === "settings") {
      loadSettings();
    } else if (tab === "kvm") {
      loadKvmStatus();
      loadActionFiles();
    }
  }

  buttons.forEach((button) => {
    button.addEventListener("click", () => activate(button.dataset.tab));
  });
}

function bindQuickActionButtons() {
  document.querySelectorAll("[data-action-key]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const code = Number(btn.dataset.actionKey);
      if (!Number.isFinite(code)) return;
      sendKeyTap(code);
    });
  });

  document.querySelectorAll("[data-action-combo]").forEach((btn) => {
    btn.addEventListener("click", () => {
      sendCombo(btn.dataset.actionCombo);
    });
  });
}

function initEvents() {
  qs("new-file-btn").addEventListener("click", createNewFile);
  qs("save-btn").addEventListener("click", saveCurrentFile);
  qs("run-btn").addEventListener("click", runScript);
  qs("delete-btn").addEventListener("click", deleteCurrentFile);
  qs("download-btn").addEventListener("click", downloadCurrentFile);
  qs("stop-btn").addEventListener("click", stopScript);

  qs("inject-btn").addEventListener("click", injectLiveText);
  qs("clear-live-btn").addEventListener("click", () => {
    qs("live-text").value = "";
    setText("remote-status", "Cleared.");
  });

  qs("add-custom-action-btn").addEventListener("click", addCustomAction);

  qs("export-pref-btn").addEventListener("click", exportPreferenceBundle);
  qs("import-pref-btn").addEventListener("click", () => qs("import-pref-input").click());
  qs("import-pref-input").addEventListener("change", async (event) => {
    const file = event.target.files && event.target.files[0];
    event.target.value = "";
    if (!file) return;

    try {
      await importPreferenceBundle(file);
    } catch (_) {
      setText("remote-status", "Invalid preference file.");
    }
  });

  qs("save-kvm-btn").addEventListener("click", saveKvmConfig);
  qs("refresh-kvm-btn").addEventListener("click", loadKvmStatus);
  qs("kvm-release-all-btn").addEventListener("click", releaseAllHid);

  qs("copy-kvm-cmd-main").addEventListener("click", () => {
    copyTextToClipboard(qs("kvm-cmd-main").value, "Shared mode command copied.");
  });
  qs("copy-kvm-cmd-no-preview").addEventListener("click", () => {
    copyTextToClipboard(qs("kvm-cmd-no-preview").value, "Exclusive mode command copied.");
  });
  qs("copy-target-capture-cmd").addEventListener("click", () => {
    copyTextToClipboard(qs("target-capture-cmd").value, "Target capture command copied.");
  });

  qs("load-screenshot-btn").addEventListener("click", loadScreenshotPreview);
  qs("open-screenshot-url-btn").addEventListener("click", () => {
    const url = qs("screenshot-url").value.trim();
    if (!url) return;
    window.open(url, "_blank", "noopener,noreferrer");
  });

  qs("screenshot-auto-refresh").addEventListener("change", syncScreenshotAutoRefresh);
  qs("screenshot-interval-ms").addEventListener("change", syncScreenshotAutoRefresh);

  qs("record-start-btn").addEventListener("click", startRecording);
  qs("record-stop-btn").addEventListener("click", stopRecording);
  qs("record-clear-btn").addEventListener("click", clearRecording);
  qs("record-download-btn").addEventListener("click", downloadRecordingFile);
  qs("record-save-device-btn").addEventListener("click", saveRecordingToDevice);
  qs("record-source").addEventListener("change", () => {
    const source = getRecordSource();
    recorder.source = source;
    if (source === "kvm_bridge") {
      loadBridgeRecordStatus();
      setText("record-status", "Source set to KVM Bridge. Click Start Recording to capture incoming packets.");
    } else {
      setText("record-status", "Source set to Web UI.");
    }
  });
  qs("action-run-btn").addEventListener("click", runSelectedActionFile);
  qs("action-delete-btn").addEventListener("click", deleteSelectedActionFile);
  qs("action-refresh-btn").addEventListener("click", loadActionFiles);

  qs("action-import-btn").addEventListener("click", () => qs("action-import-input").click());
  qs("action-import-input").addEventListener("change", async (event) => {
    const file = event.target.files && event.target.files[0];
    event.target.value = "";
    if (!file) return;

    try {
      await importActionFileToDevice(file);
    } catch (_) {
      setText("record-status", "Failed to import action file.");
    }
  });

  qs("save-settings-btn").addEventListener("click", saveSettings);
  qs("reboot-btn").addEventListener("click", rebootDevice);
  qs("logout-btn").addEventListener("click", logout);

  qs("generate-proxy-token").addEventListener("click", () => {
    qs("proxy-auth-token").value = generateRandomToken(48);
    setText("settings-status", "Generated a new proxy token. Save settings to apply.");
  });

  qs("usb-vendor-preset").addEventListener("change", () => {
    applyVendorPreset(qs("usb-vendor-preset").value);
  });

  ["usb-vendor-name", "usb-vendor-id", "usb-product-id"].forEach((id) => {
    qs(id).addEventListener("input", () => {
      qs("usb-vendor-preset").value = "custom";
    });
  });

  bindSliderReadout("typing-delay", " ms");
  bindSliderReadout("burst-chars", "");
  bindSliderReadout("burst-pause", " ms");
  bindSliderReadout("line-delay", " ms");
  bindSliderReadout("kvm-mouse-smooth", "%");

  updateKvmHelperPanel();
}

async function bootstrap() {
  setupTabs();
  loadKeyboardPrefs();
  initEvents();
  bindQuickActionButtons();
  bindHelpPanel();

  renderKeyboard65();
  bindModifierLocks();
  bindTrackpad();
  bindKeyboardPreferenceControls();
  startMouseFlushLoop();

  loadCustomActions();
  renderCustomActions();
  applyKeyboardPrefsToUI();

  await loadFiles();
  await refreshStatus();
  await loadKvmStatus();
  await loadActionFiles();
  syncScreenshotAutoRefresh();

  statusPollTimer = setInterval(() => {
    refreshStatus();
    if (activeTab === "kvm") {
      loadKvmStatus();
    }
  }, 1300);
}

window.addEventListener("beforeunload", () => {
  if (statusPollTimer) clearInterval(statusPollTimer);
  if (mouseFlushTimer) clearInterval(mouseFlushTimer);
  stopScreenshotAutoRefresh();
  cleanupScreenshotObjectUrl();
});

bootstrap();
