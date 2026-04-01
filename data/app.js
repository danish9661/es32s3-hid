let currentFile = "";
let statusPollTimer = null;
let mouseFlushTimer = null;

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

const quickActionStorageKey = "esp32_custom_quick_actions_v2";
let customQuickActions = [];
const lockedModifiers = new Set();
const pointerPressedKeys = new Map();

const trackpadPointers = new Map();
const mouseAccum = {
  dx: 0,
  dy: 0,
  wheel: 0,
  pan: 0,
};

let leftMouseHeld = false;

function qs(id) {
  return document.getElementById(id);
}

function setText(id, text) {
  const el = qs(id);
  if (el) el.textContent = text;
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

  const content = qs("code-area").value;
  const blob = new Blob([content], { type: "text/plain" });
  const url = URL.createObjectURL(blob);

  const a = document.createElement("a");
  a.href = url;
  a.download = currentFile;
  a.click();

  URL.revokeObjectURL(url);
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

async function sendKeyTap(code) {
  try {
    const res = await api("/api/live_key", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code }),
    });

    if (!res.ok) {
      setText("remote-status", "Key send failed (busy or invalid).");
      return false;
    }

    setText("remote-status", `Key sent: ${code}`);
    return true;
  } catch (_) {
    setText("remote-status", "Network error while sending key.");
    return false;
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

async function sendCombo(combo) {
  const rawParts = combo
    .toLowerCase()
    .split("+")
    .map((s) => s.trim())
    .filter(Boolean);

  if (!rawParts.length) {
    setText("remote-status", "Invalid combo.");
    return;
  }

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
  if (code == null) {
    setText("remote-status", `Unsupported combo key: ${lastToken}`);
    return;
  }

  const payload = {
    ...mods,
    code,
  };

  try {
    const res = await api("/api/live_combo", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    if (!res.ok) {
      setText("remote-status", "Combo failed (busy or invalid).");
      return;
    }

    setText("remote-status", `Combo sent: ${combo}`);
  } catch (_) {
    setText("remote-status", "Network error while sending combo.");
  }
}

async function sendKeyboardEvent(action, code = null, hold = 30) {
  const payload = { action };
  if (code != null) payload.code = code;
  if (hold != null) payload.hold = hold;

  const res = await api("/api/kbd_event", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });

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
    customQuickActions = Array.isArray(parsed) ? parsed : [];
  } catch (_) {
    customQuickActions = [];
  }
}

function saveCustomActions() {
  localStorage.setItem(quickActionStorageKey, JSON.stringify(customQuickActions));
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

  const code = parseKeyToken(combo.split("+").pop().trim());
  if (code == null) {
    alert("Invalid shortcut key.");
    return;
  }

  customQuickActions.push({ label, combo });
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
          sendKeyboardEvent("down", code)
            .then((ok) => {
              if (!ok) return;
              pointerPressedKeys.set(event.pointerId, { code, keyEl, label: keyDef.label });
              keyEl.classList.add("active");
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

          sendKeyboardEvent("up", active.code)
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
          await sendKeyboardEvent("up", code);
          lockedModifiers.delete(code);
          button.classList.remove("active");
          setText("keyboard-status", "Modifier unlocked.");
        } catch (_) {
          setText("keyboard-status", "Failed to unlock modifier.");
        }
      } else {
        try {
          await sendKeyboardEvent("down", code);
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
      await sendKeyboardEvent("release_all");
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
      }).catch(() => {});
    }

    if (wheel !== 0 || pan !== 0) {
      mouseAccum.wheel -= wheel;
      mouseAccum.pan -= pan;

      api("/api/mouse_scroll", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ wheel, pan }),
      }).catch(() => {});
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

    trackpadPointers.set(event.pointerId, { x: event.clientX, y: event.clientY });

    if (trackpadPointers.size >= 2) {
      mouseAccum.wheel += (-dy * 0.2);
      setText("mouse-status", "Gesture scroll.");
    } else {
      mouseAccum.dx += (dx * 1.45);
      mouseAccum.dy += (dy * 1.45);
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
      await api("/api/mouse_button", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ button: "left", action: "click" }),
      });
      setText("mouse-status", "Left click sent.");
    } catch (_) {
      setText("mouse-status", "Left click failed.");
    }
  });

  qs("mouse-right-btn").addEventListener("click", async () => {
    try {
      await api("/api/mouse_button", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ button: "right", action: "click" }),
      });
      setText("mouse-status", "Right click sent.");
    } catch (_) {
      setText("mouse-status", "Right click failed.");
    }
  });

  qs("mouse-left-hold-btn").addEventListener("click", async () => {
    try {
      if (!leftMouseHeld) {
        await api("/api/mouse_button", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ button: "left", action: "down" }),
        });
        leftMouseHeld = true;
        qs("mouse-left-hold-btn").textContent = "Release Left";
        setText("mouse-status", "Left button held.");
      } else {
        await api("/api/mouse_button", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ button: "left", action: "up" }),
        });
        leftMouseHeld = false;
        qs("mouse-left-hold-btn").textContent = "Hold Left";
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
    qs("led-bright").value = d.bright ?? 50;

    qs("login-rate-limit").checked = d.login_rate_limit ?? true;
    qs("proxy-auth-enabled").checked = d.proxy_auth_enabled ?? false;
    qs("proxy-auth-token").value = d.proxy_auth_token || "";

    updateSliderReadout("typing-delay", " ms");
    updateSliderReadout("burst-chars", "");
    updateSliderReadout("burst-pause", " ms");
    updateSliderReadout("line-delay", " ms");

    setText("settings-status", "Settings loaded.");
  } catch (_) {
    setText("settings-status", "Network error while loading settings.");
  }
}

async function saveSettings() {
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
    bright: Number(qs("led-bright").value),

    login_rate_limit: qs("login-rate-limit").checked,
    proxy_auth_enabled: qs("proxy-auth-enabled").checked,
    proxy_auth_token: qs("proxy-auth-token").value.trim(),
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

    qs("admin-pass").value = "";
    setText("settings-status", "Settings saved.");
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

  function activate(tab) {
    buttons.forEach((b) => b.classList.toggle("active", b.dataset.tab === tab));
    views.forEach((v) => v.classList.toggle("active", v.id === `view-${tab}`));

    if (tab === "settings") {
      loadSettings();
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

  qs("save-settings-btn").addEventListener("click", saveSettings);
  qs("reboot-btn").addEventListener("click", rebootDevice);
  qs("logout-btn").addEventListener("click", logout);

  qs("generate-proxy-token").addEventListener("click", () => {
    qs("proxy-auth-token").value = generateRandomToken(48);
    setText("settings-status", "Generated a new proxy token. Save settings to apply.");
  });

  bindSliderReadout("typing-delay", " ms");
  bindSliderReadout("burst-chars", "");
  bindSliderReadout("burst-pause", " ms");
  bindSliderReadout("line-delay", " ms");
}

async function bootstrap() {
  setupTabs();
  initEvents();
  bindQuickActionButtons();
  bindHelpPanel();

  renderKeyboard65();
  bindModifierLocks();
  bindTrackpad();
  startMouseFlushLoop();

  loadCustomActions();
  renderCustomActions();

  await loadFiles();
  await refreshStatus();

  statusPollTimer = setInterval(refreshStatus, 1300);
}

window.addEventListener("beforeunload", () => {
  if (statusPollTimer) clearInterval(statusPollTimer);
  if (mouseFlushTimer) clearInterval(mouseFlushTimer);
});

bootstrap();
