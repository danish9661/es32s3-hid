let currentFile = "";
let statusPollTimer = null;

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

async function sendKey(code) {
  try {
    const res = await api("/api/live_key", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code }),
    });

    if (!res.ok) {
      setText("remote-status", "Key send failed (busy or invalid).");
      return;
    }

    setText("remote-status", `Key sent: ${code}`);
  } catch (_) {
    setText("remote-status", "Network error while sending key.");
  }
}

async function sendCombo(combo) {
  const lower = combo.toLowerCase();
  const parts = lower.split("+");
  const payload = {
    ctrl: parts.includes("ctrl"),
    alt: parts.includes("alt"),
    shift: parts.includes("shift"),
    gui: parts.includes("gui") || parts.includes("win"),
  };

  const keyPart = parts[parts.length - 1];
  if (!keyPart) return;

  if (/^[a-z0-9]$/.test(keyPart)) {
    payload.char = keyPart;
  } else if (keyPart === "f4") {
    payload.code = 197;
  } else {
    setText("remote-status", `Unsupported combo key: ${keyPart}`);
    return;
  }

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

function bindQuickActions() {
  document.querySelectorAll("[data-key]").forEach((el) => {
    el.addEventListener("click", () => {
      const code = Number(el.getAttribute("data-key"));
      sendKey(code);
    });
  });

  document.querySelectorAll("[data-combo]").forEach((el) => {
    el.addEventListener("click", () => {
      sendCombo(el.getAttribute("data-combo"));
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
  qs("save-settings-btn").addEventListener("click", saveSettings);
  qs("reboot-btn").addEventListener("click", rebootDevice);
  qs("logout-btn").addEventListener("click", logout);
}

async function bootstrap() {
  setupTabs();
  bindQuickActions();
  initEvents();

  await loadFiles();
  await refreshStatus();

  statusPollTimer = setInterval(refreshStatus, 1300);
}

window.addEventListener("beforeunload", () => {
  if (statusPollTimer) clearInterval(statusPollTimer);
});

bootstrap();
