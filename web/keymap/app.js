/* 知言 keymap config — Web Serial CDC */

const DEFAULT = {
  voice: { modifiers: 0x90, keycode: 0x00 }, // RCtrl|RGui
  yes: { modifiers: 0x00, keycode: 0x28 },   // Enter
  no: { modifiers: 0x00, keycode: 0x2a },    // Backspace
};

const CODE_TO_HID = {
  KeyA: 0x04, KeyB: 0x05, KeyC: 0x06, KeyD: 0x07, KeyE: 0x08, KeyF: 0x09,
  KeyG: 0x0a, KeyH: 0x0b, KeyI: 0x0c, KeyJ: 0x0d, KeyK: 0x0e, KeyL: 0x0f,
  KeyM: 0x10, KeyN: 0x11, KeyO: 0x12, KeyP: 0x13, KeyQ: 0x14, KeyR: 0x15,
  KeyS: 0x16, KeyT: 0x17, KeyU: 0x18, KeyV: 0x19, KeyW: 0x1a, KeyX: 0x1b,
  KeyY: 0x1c, KeyZ: 0x1d,
  Digit1: 0x1e, Digit2: 0x1f, Digit3: 0x20, Digit4: 0x21, Digit5: 0x22,
  Digit6: 0x23, Digit7: 0x24, Digit8: 0x25, Digit9: 0x26, Digit0: 0x27,
  Enter: 0x28, Escape: 0x29, Backspace: 0x2a, Tab: 0x2b, Space: 0x2c,
  Minus: 0x2d, Equal: 0x2e, BracketLeft: 0x2f, BracketRight: 0x30,
  Backslash: 0x31, Semicolon: 0x33, Quote: 0x34, Backquote: 0x35,
  Comma: 0x36, Period: 0x37, Slash: 0x38, CapsLock: 0x39,
  F1: 0x3a, F2: 0x3b, F3: 0x3c, F4: 0x3d, F5: 0x3e, F6: 0x3f,
  F7: 0x40, F8: 0x41, F9: 0x42, F10: 0x43, F11: 0x44, F12: 0x45,
  PrintScreen: 0x46, ScrollLock: 0x47, Pause: 0x48, Insert: 0x49,
  Home: 0x4a, PageUp: 0x4b, Delete: 0x4c, End: 0x4d, PageDown: 0x4e,
  ArrowRight: 0x4f, ArrowLeft: 0x50, ArrowDown: 0x51, ArrowUp: 0x52,
};

const IS_MAC = /Mac|iPhone|iPad/i.test(navigator.platform || navigator.userAgent || "");

/** HID modifier bits — labels follow host OS. */
const MOD_GROUPS = IS_MAC
  ? [
      {
        label: "⌘",
        bits: [
          { bit: 0x08, label: "左 ⌘" },
          { bit: 0x80, label: "右 ⌘" },
        ],
      },
      {
        label: "⌥",
        bits: [
          { bit: 0x04, label: "左 ⌥" },
          { bit: 0x40, label: "右 ⌥" },
        ],
      },
      {
        label: "Shift",
        bits: [
          { bit: 0x02, label: "左 Shift" },
          { bit: 0x20, label: "右 Shift" },
        ],
      },
      {
        label: "Ctrl",
        bits: [
          { bit: 0x01, label: "左 Ctrl" },
          { bit: 0x10, label: "右 Ctrl" },
        ],
      },
    ]
  : [
      {
        label: "Win",
        bits: [
          { bit: 0x08, label: "左 Win" },
          { bit: 0x80, label: "右 Win" },
        ],
      },
      {
        label: "Alt",
        bits: [
          { bit: 0x04, label: "左 Alt" },
          { bit: 0x40, label: "右 Alt" },
        ],
      },
      {
        label: "Shift",
        bits: [
          { bit: 0x02, label: "左 Shift" },
          { bit: 0x20, label: "右 Shift" },
        ],
      },
      {
        label: "Ctrl",
        bits: [
          { bit: 0x01, label: "左 Ctrl" },
          { bit: 0x10, label: "右 Ctrl" },
        ],
      },
    ];

const MOD_NAMES = MOD_GROUPS.flatMap((g) => g.bits.map((b) => [b.bit, b.label]));

const HID_NAMES = Object.fromEntries(
  Object.entries(CODE_TO_HID).map(([k, v]) => [v, k.replace(/^Key|^Digit/, "")])
);
HID_NAMES[0x28] = "Enter";
HID_NAMES[0x2a] = "Backspace";
HID_NAMES[0x2c] = "Space";
HID_NAMES[0x29] = "Esc";

const PRESETS = [
  {
    label: IS_MAC ? "右Ctrl+右⌥" : "右Ctrl+右Alt",
    slot: "voice",
    binding: { modifiers: 0x50, keycode: 0x00 }, // RCtrl|RAlt
  },
  {
    label: IS_MAC ? "豆包（右Ctrl+右⌘）" : "豆包（右Ctrl+右Win）",
    slot: "voice",
    binding: { ...DEFAULT.voice },
  },
  { label: "Enter", slot: "yes", binding: { ...DEFAULT.yes } },
  { label: "Backspace", slot: "no", binding: { ...DEFAULT.no } },
  { label: "Space", slot: "yes", binding: { modifiers: 0, keycode: 0x2c } },
  { label: "Esc", slot: "no", binding: { modifiers: 0, keycode: 0x29 } },
];

const SLOT_TITLE = { voice: "Voice", yes: "Yes", no: "No" };

let port = null;
let reader = null;
let writer = null;
let readBuf = "";
let map = structuredClone(DEFAULT);
let focusSlot = "voice";
let recording = null; // slot name
let peakMods = 0;
let modsDown = new Set();
/** Mirrors map[focusSlot].modifiers for chip UI + optional keyboard capture. */
let stickyMods = 0;
/** PWM duty 0–255 — last value we intend / show in the UI. */
let blDuty = 64;
/** True while pointer is down on the slider — never fight the thumb. */
let blDragging = false;
let blTimer = null;
let blSending = false;
/** Latest duty requested while a send is in flight. */
let blPending = null;
/** Physical handheld key state from CDC `key N down|up`. */
const hwDown = { voice: false, yes: false, no: false };
/** Pointer press on the on-page mock. */
const uiDown = { voice: false, yes: false, no: false };
const KEY_ID_TO_SLOT = ["voice", "yes", "no"];
let cdcDrainTimer = null;

/* —— 160×80 handheld LCD preview (mirrors firmware vk_ui) —— */
const LCD = {
  W: 160,
  H: 80,
  BG: "#0b0c10",
  TIME: "#dcbc8c",
  MUTED: "#6e7480",
  DIM: "#30343e",
  FRAME: "#a0a8b4",
  SAGE: "#60c484",
  CLAY: "#dc6058",
  WARM: "#ecb048",
  TALK: "#e89878",
  YES: "#6ec88c",
  NO: "#e8786c",
  SEG_OFF: "#1c2028",
};
const DIGIT_3X5 = [
  [0x7, 0x5, 0x5, 0x5, 0x7],
  [0x2, 0x6, 0x2, 0x2, 0x7],
  [0x7, 0x1, 0x7, 0x4, 0x7],
  [0x7, 0x1, 0x7, 0x1, 0x7],
  [0x5, 0x5, 0x7, 0x1, 0x1],
  [0x7, 0x4, 0x7, 0x1, 0x7],
  [0x7, 0x4, 0x7, 0x5, 0x7],
  [0x7, 0x1, 0x1, 0x1, 0x1],
  [0x7, 0x5, 0x7, 0x5, 0x7],
  [0x7, 0x5, 0x7, 0x1, 0x7],
];
/* 5×7 column-major bit0=top for a–z / space (subset used for flash text) */
const FONT5X7 = {
  " ": [0, 0, 0, 0, 0],
  a: [0x20, 0x54, 0x54, 0x54, 0x78],
  e: [0x38, 0x54, 0x54, 0x54, 0x18],
  i: [0x00, 0x44, 0x7d, 0x40, 0x00],
  k: [0x7f, 0x10, 0x28, 0x44, 0x00],
  l: [0x00, 0x41, 0x7f, 0x40, 0x00],
  n: [0x7c, 0x08, 0x04, 0x04, 0x78],
  o: [0x38, 0x44, 0x44, 0x44, 0x38],
  p: [0x7c, 0x14, 0x14, 0x14, 0x08],
  r: [0x7c, 0x08, 0x04, 0x04, 0x08],
  s: [0x48, 0x54, 0x54, 0x54, 0x20],
  t: [0x04, 0x3f, 0x44, 0x40, 0x20],
  w: [0x3c, 0x40, 0x30, 0x40, 0x3c],
  y: [0x0c, 0x50, 0x50, 0x50, 0x3c],
  "%": [0x23, 0x13, 0x08, 0x64, 0x62],
  "0": [0x3e, 0x51, 0x49, 0x45, 0x3e],
  "1": [0x00, 0x42, 0x7f, 0x40, 0x00],
  "2": [0x42, 0x61, 0x51, 0x49, 0x46],
  "3": [0x21, 0x41, 0x45, 0x4b, 0x31],
  "4": [0x18, 0x14, 0x12, 0x7f, 0x10],
  "5": [0x27, 0x45, 0x45, 0x45, 0x39],
  "6": [0x3c, 0x4a, 0x49, 0x49, 0x30],
  "7": [0x01, 0x71, 0x09, 0x05, 0x03],
  "8": [0x36, 0x49, 0x49, 0x49, 0x36],
  "9": [0x06, 0x49, 0x49, 0x29, 0x1e],
};

let lcdMode = "idle"; /* idle | talk | yes | no | pair | away */
let lcdLinked = false;
let lcdBatPct = 87;
let lcdFlashTimer = null;
let lcdClockTimer = null;
let statusTimer = null;
let lcdUnix = 0; /* last status/sync unix from device; 0 = no wall clock yet */
let lcdUnixAt = 0; /* Date.now() when lcdUnix was captured */
const waitPupImg = new Image();
waitPupImg.src = "wait.png";
waitPupImg.onload = () => {
  if (typeof paintLcd === "function") paintLcd();
};

function lcdCtx() {
  const c = $("lcdCanvas");
  return c ? c.getContext("2d") : null;
}

function lcdFill(ctx, color) {
  ctx.fillStyle = color;
  ctx.fillRect(0, 0, LCD.W, LCD.H);
}

function lcdRect(ctx, x, y, w, h, color) {
  ctx.fillStyle = color;
  ctx.fillRect(x, y, w, h);
}

function lcdBlock(ctx, x, y, cell, color) {
  const s = Math.max(1, cell - 1);
  lcdRect(ctx, x, y, s, s, color);
}

function lcdDigit(ctx, x, y, digit, cell, color) {
  const rows = DIGIT_3X5[digit];
  if (!rows) return;
  for (let row = 0; row < 5; row++) {
    for (let col = 0; col < 3; col++) {
      if (rows[row] & (1 << (2 - col))) {
        lcdBlock(ctx, x + col * cell, y + row * cell, cell, color);
      }
    }
  }
}

function lcdColon(ctx, x, y, cell, color) {
  const s = Math.max(2, cell - 1);
  lcdRect(ctx, x, y + cell, s, s, color);
  lcdRect(ctx, x, y + cell * 3, s, s, color);
}

function lcdClock(ctx, hour, minute, valid) {
  const cell = 6;
  const gw = 3 * cell;
  const gap = 4;
  const colonW = cell;
  const total = gw * 4 + colonW + gap * 4;
  let x = Math.floor((LCD.W - total) / 2);
  const y = 26;
  const c = valid ? LCD.TIME : LCD.MUTED;
  if (!valid) {
    for (let i = 0; i < 4; i++) {
      let gx = x + i * (gw + gap);
      if (i >= 2) gx += colonW + gap;
      lcdRect(ctx, gx + cell / 2, y + 2 * cell, gw - cell, cell - 1, c);
    }
    lcdColon(ctx, x + 2 * (gw + gap), y, cell, LCD.DIM);
    return;
  }
  lcdDigit(ctx, x, y, Math.floor(hour / 10), cell, c);
  x += gw + gap;
  lcdDigit(ctx, x, y, hour % 10, cell, c);
  x += gw + gap;
  lcdColon(ctx, x, y, cell, LCD.WARM);
  x += colonW + gap;
  lcdDigit(ctx, x, y, Math.floor(minute / 10), cell, c);
  x += gw + gap;
  lcdDigit(ctx, x, y, minute % 10, cell, c);
}

function lcdGlyph(ctx, x, y, ch, scale, color) {
  const g = FONT5X7[ch] || FONT5X7[" "];
  for (let col = 0; col < 5; col++) {
    for (let row = 0; row < 7; row++) {
      if (g[col] & (1 << row)) {
        lcdRect(ctx, x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

function lcdText(ctx, x, y, text, scale, gap, color) {
  let cx = x;
  for (const ch of text) {
    lcdGlyph(ctx, cx, y, ch, scale, color);
    cx += 5 * scale + gap;
  }
}

function lcdTextCentered(ctx, y, text, scale, gap, color) {
  const n = text.length;
  const w = n * (5 * scale) + (n - 1) * gap;
  lcdText(ctx, Math.floor((LCD.W - w) / 2), y, text, scale, gap, color);
}

function lcdPip(ctx, linked, pairing) {
  const x = 8;
  const y = 6;
  const fill = pairing ? LCD.WARM : linked ? LCD.SAGE : LCD.DIM;
  lcdRect(ctx, x + 1, y, 3, 5, fill);
  lcdRect(ctx, x, y + 1, 5, 3, fill);
  if (!linked && !pairing) {
    lcdRect(ctx, x + 1, y + 1, 3, 3, LCD.BG);
    lcdRect(ctx, x + 2, y + 2, 1, 1, LCD.MUTED);
  }
}

function lcdBattery(ctx, pct) {
  pct = Math.max(0, Math.min(100, pct | 0));
  const label = `${pct}%`;
  /* 5px glyph + 1px gap → width ≈ n*6 - 1 */
  const tw = label.length * 6 - 1;
  const iconX = 128;
  const iconY = 4;
  const iconW = 26;
  const iconH = 12;
  lcdText(ctx, iconX - tw - 3, 6, label, 1, 1, LCD.FRAME);

  lcdRect(ctx, iconX, iconY, iconW, 1, LCD.FRAME);
  lcdRect(ctx, iconX, iconY + iconH - 1, iconW, 1, LCD.FRAME);
  lcdRect(ctx, iconX, iconY, 1, iconH, LCD.FRAME);
  lcdRect(ctx, iconX + iconW - 1, iconY, 1, iconH, LCD.FRAME);
  lcdRect(ctx, iconX + iconW, iconY + 3, 3, iconH - 6, LCD.FRAME);

  let segs = 0;
  if (pct >= 75) segs = 4;
  else if (pct >= 50) segs = 3;
  else if (pct >= 25) segs = 2;
  else if (pct > 0) segs = 1;
  const fillc = pct < 20 ? LCD.CLAY : pct < 50 ? LCD.WARM : LCD.SAGE;
  for (let i = 0; i < 4; i++) {
    const sx = iconX + 3 + i * 5;
    lcdRect(ctx, sx, iconY + 3, 4, iconH - 6, i < segs ? fillc : LCD.SEG_OFF);
  }
}

function lcdStatusBar(ctx, linked, pairing) {
  const c = pairing ? LCD.WARM : linked ? LCD.SAGE : LCD.DIM;
  lcdRect(ctx, 20, 74, 120, 2, c);
}

function lcdNowHM() {
  if (lcdUnix > 1000000000) {
    const unix = lcdUnix + Math.floor((Date.now() - lcdUnixAt) / 1000);
    /* Device reports UTC epoch; display UTC+8 like firmware default. */
    const local = new Date((unix + 8 * 3600) * 1000);
    return {
      hour: local.getUTCHours(),
      minute: local.getUTCMinutes(),
      valid: true,
    };
  }
  return { hour: 0, minute: 0, valid: false };
}

function lcdWaitPup(ctx) {
  if (waitPupImg.complete && waitPupImg.naturalWidth > 0) {
    ctx.drawImage(waitPupImg, 0, 0, LCD.W, LCD.H);
    return;
  }
  lcdTextCentered(ctx, 34, "wait", 2, 2, LCD.MUTED);
}

function paintLcd() {
  const ctx = lcdCtx();
  if (!ctx) return;
  const pairing = lcdMode === "pair";
  const { hour, minute, valid } = lcdNowHM();
  const showWait = lcdMode === "away" || (lcdMode === "idle" && !valid);

  if (showWait) {
    lcdWaitPup(ctx);
    return;
  }

  lcdFill(ctx, LCD.BG);
  lcdPip(ctx, lcdLinked, pairing);
  lcdBattery(ctx, lcdBatPct);
  lcdStatusBar(ctx, lcdLinked, pairing);

  switch (lcdMode) {
    case "pair":
      lcdTextCentered(ctx, 32, "pair", 3, 3, LCD.WARM);
      break;
    case "talk":
      lcdTextCentered(ctx, 30, "talk", 3, 3, LCD.TALK);
      lcdRect(ctx, 48, 60, 64, 3, LCD.DIM);
      lcdRect(ctx, 48, 60, 40, 3, LCD.TALK);
      break;
    case "yes":
      lcdTextCentered(ctx, 30, "yes", 3, 4, LCD.YES);
      break;
    case "no":
      lcdTextCentered(ctx, 30, "no", 3, 4, LCD.NO);
      break;
    default:
      lcdClock(ctx, hour, minute, valid);
      break;
  }
}

function setLcdMode(mode, flashMs) {
  lcdMode = mode;
  paintLcd();
  if (lcdFlashTimer) {
    clearTimeout(lcdFlashTimer);
    lcdFlashTimer = null;
  }
  if (flashMs) {
    lcdFlashTimer = setTimeout(() => {
      lcdFlashTimer = null;
      lcdMode = "idle";
      paintLcd();
    }, flashMs);
  }
}

function renderBl({ forceSlider = false } = {}) {
  const pct = dutyToPercent(blDuty);
  const range = $("blRange");
  const label = $("blValue");
  /* While dragging, the browser owns the thumb — rewriting value causes jump-back. */
  if (range && (forceSlider || (!blDragging && document.activeElement !== range))) {
    range.value = String(pct);
  }
  if (label) label.textContent = `${pct}%`;
  const screen = $("deviceScreen");
  if (screen) screen.style.setProperty("--bl", String(Math.max(0, Math.min(1, blDuty / 255))));
}

function parseWireBl(line) {
  const m = String(line).trim().match(/^B\s*=\s*(\d+)/i);
  if (m) return Math.max(0, Math.min(255, Number(m[1])));
  const m2 = String(line).trim().match(/\bB\s+(\d+)\b/i);
  if (m2) return Math.max(0, Math.min(255, Number(m2[1])));
  return null;
}

const NAME_TO_HID = {
  ...Object.fromEntries(
    Object.entries(CODE_TO_HID).flatMap(([code, hid]) => {
      const short = code.replace(/^Key|^Digit/, "").toLowerCase();
      return [[short, hid], [code.toLowerCase(), hid]];
    })
  ),
  enter: 0x28, return: 0x28, esc: 0x29, escape: 0x29,
  backspace: 0x2a, bksp: 0x2a, delete: 0x4c, tab: 0x2b, space: 0x2c, " ": 0x2c,
};

const $ = (id) => document.getElementById(id);
const log = (msg) => { $("log").textContent = msg; };

function eventMods(e) {
  let m = 0;
  if (e.ctrlKey) m |= e.location === 2 ? 0x10 : 0x01;
  if (e.shiftKey) m |= e.location === 2 ? 0x20 : 0x02;
  if (e.altKey) m |= e.location === 2 ? 0x40 : 0x04;
  if (e.metaKey) m |= e.location === 2 ? 0x80 : 0x08;
  /* Prefer explicit code for left/right when available */
  if (e.code === "ControlLeft") m = (m & ~0x11) | 0x01;
  if (e.code === "ControlRight") m = (m & ~0x11) | 0x10;
  if (e.code === "ShiftLeft") m = (m & ~0x22) | 0x02;
  if (e.code === "ShiftRight") m = (m & ~0x22) | 0x20;
  if (e.code === "AltLeft") m = (m & ~0x44) | 0x04;
  if (e.code === "AltRight") m = (m & ~0x44) | 0x40;
  if (e.code === "MetaLeft") m = (m & ~0x88) | 0x08;
  if (e.code === "MetaRight") m = (m & ~0x88) | 0x80;
  return m;
}

function isModifierCode(code) {
  return /^(Control|Shift|Alt|Meta)(Left|Right)$/.test(code);
}

function formatBinding(b) {
  if (!b || (b.modifiers === 0 && b.keycode === 0)) return "（空）";
  const parts = [];
  for (const [bit, name] of MOD_NAMES) {
    if (b.modifiers & bit) parts.push(name);
  }
  if (b.keycode) {
    parts.push(HID_NAMES[b.keycode] || `0x${b.keycode.toString(16)}`);
  }
  return parts.join(" + ");
}

function bindingToWire(b) {
  return `${b.modifiers.toString(16).padStart(2, "0")}:${b.keycode.toString(16).padStart(2, "0")}`.toUpperCase();
}

function parseWireMap(line) {
  /* K=90:00,00:28,00:2A */
  const m = line.match(/K[= ]?\s*([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}),([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2}),([0-9A-Fa-f]{2}):([0-9A-Fa-f]{2})/);
  if (!m) return null;
  return {
    voice: { modifiers: parseInt(m[1], 16), keycode: parseInt(m[2], 16) },
    yes: { modifiers: parseInt(m[3], 16), keycode: parseInt(m[4], 16) },
    no: { modifiers: parseInt(m[5], 16), keycode: parseInt(m[6], 16) },
  };
}

function dutyToPercent(duty) {
  return Math.round((Number(duty) / 255) * 100);
}

function percentToDuty(pct) {
  const p = Math.max(0, Math.min(100, Number(pct) || 0));
  return Math.round((p / 100) * 255);
}

function render() {
  for (const slot of ["voice", "yes", "no"]) {
    $(`label-${slot}`).textContent = formatBinding(map[slot]);
    const card = document.querySelector(`.keycard[data-slot="${slot}"]`);
    card.classList.toggle("focus", focusSlot === slot);
    const recBtn = card.querySelector("[data-rec]");
    recBtn.classList.toggle("recording", recording === slot);
    recBtn.textContent = recording === slot ? "录制中…" : "键盘录制";
  }
  const nameEl = $("focusName");
  if (nameEl) nameEl.textContent = SLOT_TITLE[focusSlot] || focusSlot;
  stickyMods = map[focusSlot]?.modifiers || 0;
  document.querySelectorAll("#modChips [data-mod]").forEach((btn) => {
    const bit = Number(btn.dataset.mod);
    btn.classList.toggle("on", (stickyMods & bit) !== 0);
  });
  syncKeyCaps();
  renderBl();
}

function selectSlot(slot) {
  focusSlot = slot;
  stickyMods = map[slot]?.modifiers || 0;
  if (recording && recording !== slot) stopRecord();
  render();
}

function toggleModBit(bit) {
  const prev = map[focusSlot] || { modifiers: 0, keycode: 0 };
  const nextMods = prev.modifiers ^ bit;
  map[focusSlot] = { modifiers: nextMods, keycode: prev.keycode };
  stickyMods = nextMods;
  render();
  log(`${SLOT_TITLE[focusSlot]} → ${formatBinding(map[focusSlot])} · 记得「保存到设备」`);
}

function clearModsKeepKey() {
  const prev = map[focusSlot] || { modifiers: 0, keycode: 0 };
  map[focusSlot] = { modifiers: 0, keycode: prev.keycode };
  stickyMods = 0;
  render();
  log(`${SLOT_TITLE[focusSlot]} → ${formatBinding(map[focusSlot])}`);
}

function clearKeyKeepMods() {
  const prev = map[focusSlot] || { modifiers: 0, keycode: 0 };
  map[focusSlot] = { modifiers: prev.modifiers, keycode: 0 };
  stickyMods = prev.modifiers;
  render();
  log(`${SLOT_TITLE[focusSlot]} → ${formatBinding(map[focusSlot])}`);
}

function syncKeyCaps() {
  document.querySelectorAll(".vk-key[data-slot]").forEach((cap) => {
    const slot = cap.dataset.slot;
    cap.classList.toggle("focus", focusSlot === slot);
    cap.classList.toggle("pressed", !!(hwDown[slot] || uiDown[slot]));
  });
}

function applyHwKey(id, down) {
  const slot = KEY_ID_TO_SLOT[id];
  if (!slot) return;
  const was = hwDown[slot];
  hwDown[slot] = down;
  if (down && !was) {
    if (slot === "voice") setLcdMode("talk");
    else if (slot === "yes") setLcdMode("yes");
    else if (slot === "no") setLcdMode("no");
  } else if (!down && was) {
    if (slot === "voice" && lcdMode === "talk" && !uiDown.voice) setLcdMode("idle");
    else if (slot === "yes" && lcdMode === "yes" && !uiDown.yes) setLcdMode("idle");
    else if (slot === "no" && lcdMode === "no" && !uiDown.no) setLcdMode("idle");
  }
  syncKeyCaps();
}

/** Handle async CDC lines; returns true if consumed (not a command reply). */
function handleCdcLine(line) {
  const m = String(line).trim().match(/^key\s+(\d+)\s+(down|up)\b/i);
  if (!m) return false;
  applyHwKey(Number(m[1]), m[2].toLowerCase() === "down");
  return true;
}

function drainCdc() {
  if (!readBuf) return;
  if (!readBuf.includes("\n") && !readBuf.includes("\r")) return;
  const lines = readBuf.split(/\r?\n/);
  readBuf = lines.pop() || "";
  for (const line of lines) handleCdcLine(line);
}

function clearCdcForCmd() {
  drainCdc();
  readBuf = "";
}

function stopCdcDrain() {
  if (cdcDrainTimer) {
    clearInterval(cdcDrainTimer);
    cdcDrainTimer = null;
  }
}

function startCdcDrain() {
  stopCdcDrain();
  cdcDrainTimer = setInterval(drainCdc, 40);
}

/** Only a normal key — modifiers come from chips. */
function parseKeyOnly(text) {
  const raw = text.trim().toLowerCase();
  if (!raw) return null;
  if (NAME_TO_HID[raw] != null) return NAME_TO_HID[raw];
  if (raw.length === 1 && NAME_TO_HID[raw] != null) return NAME_TO_HID[raw];
  return null;
}

function applyStickyWithKey() {
  const keycode = parseKeyOnly($("keyInput").value);
  if (keycode == null) {
    log("普通键无法识别：填 V、Enter、Space 等（修饰键请点上面的按钮）");
    return;
  }
  const binding = { modifiers: map[focusSlot]?.modifiers || 0, keycode };
  map[focusSlot] = binding;
  stickyMods = binding.modifiers;
  render();
  log(`${SLOT_TITLE[focusSlot]} → ${formatBinding(binding)} · 记得「保存到设备」`);
}

function setConnected(on) {
  $("status").textContent = on ? "已连接" : "未连接";
  $("status").classList.toggle("on", on);
  $("status").classList.toggle("off", !on);
  const btnIn = $("btnConnect");
  const btnOut = $("btnDisconnect");
  if (btnIn) {
    btnIn.hidden = !!on;
    btnIn.disabled = false;
  }
  if (btnOut) {
    btnOut.hidden = !on;
  }
  $("btnPickPort").hidden = !!on;
  $("btnLoad").disabled = !on;
  $("btnSave").disabled = !on;
  $("btnReset").disabled = !on;
  $("blRange").disabled = !on;
  const idleEl = $("idleBlank");
  if (idleEl) idleEl.disabled = !on;
  const ecoEl = $("ecoRadio");
  if (ecoEl) ecoEl.disabled = !on;
  if (!on) {
    lcdLinked = false;
    if (!lcdFlashTimer) lcdMode = "idle";
    paintLcd();
  }
}

/** After serial picker closes, focus would stick on Connect→Disconnect — move it away. */
function focusAfterConnect() {
  const dangerous = [$("btnConnect"), $("btnDisconnect"), $("btnPickPort")];
  for (const el of dangerous) {
    if (el && document.activeElement === el) el.blur();
  }
  const voice = document.querySelector('.keycard[data-slot="voice"]');
  if (voice) {
    if (!voice.hasAttribute("tabindex")) voice.tabIndex = 0;
    try {
      voice.focus({ preventScroll: true });
    } catch (_) {
      voice.focus();
    }
  }
}

async function writeLine(s) {
  if (!writer) throw new Error("not connected");
  await writer.write(new TextEncoder().encode(s.endsWith("\n") ? s : s + "\n"));
}

async function readUntil(pred, ms = 2000) {
  const start = Date.now();
  while (Date.now() - start < ms) {
    const lines = readBuf.split(/\r?\n/);
    readBuf = lines.pop() || "";
    for (const line of lines) {
      if (handleCdcLine(line)) continue;
      if (pred(line)) return line;
    }
    await new Promise((r) => setTimeout(r, 40));
  }
  return null;
}

async function pumpRead() {
  const dec = new TextDecoder();
  try {
    while (port && reader) {
      const { value, done } = await reader.read();
      if (done) break;
      if (value) readBuf += dec.decode(value, { stream: true });
    }
  } catch (_) {
    /* disconnected */
  }
}

function parseWireStatus(line) {
  /* S=1,87,01,1710000000 */
  const m = String(line).trim().match(/^S=(\d+)\s*,\s*(\d+)\s*,\s*([0-9A-Fa-f]+)\s*,\s*(\d+)/);
  if (!m) return null;
  return {
    linked: Number(m[1]) === 1,
    battery: Number(m[2]),
    flags: parseInt(m[3], 16),
    unix: Number(m[4]),
  };
}

async function syncTimeToDevice() {
  const unix = Math.floor(Date.now() / 1000);
  clearCdcForCmd();
  await writeLine(`T${unix}`);
  const line = await readUntil((l) => l.includes("time set") || l.startsWith("T=") || l.includes("T err"), 2000);
  if (!line || line.includes("err")) {
    log("授时失败");
    return false;
  }
  lcdUnix = unix;
  lcdUnixAt = Date.now();
  paintLcd();
  log(`已授时 ${new Date(unix * 1000).toLocaleString()}`);
  return true;
}

async function loadStatus() {
  if (!writer) return null;
  clearCdcForCmd();
  await writeLine("S?");
  const line = await readUntil((l) => /^S=/.test(l.trim()), 1500);
  const st = line && parseWireStatus(line);
  if (!st) return null;
  lcdLinked = st.linked;
  if (Number.isFinite(st.battery)) lcdBatPct = st.battery;
  if (st.unix > 1000000000) {
    lcdUnix = st.unix;
    lcdUnixAt = Date.now();
  }
  if (!lcdFlashTimer && (lcdMode === "idle" || lcdMode === "away")) {
    lcdMode = st.linked ? "idle" : "away";
  }
  paintLcd();
  return st;
}

function stopStatusPoll() {
  if (statusTimer) {
    clearInterval(statusTimer);
    statusTimer = null;
  }
  stopCdcDrain();
}

function startStatusPoll() {
  stopStatusPoll();
  startCdcDrain();
  statusTimer = setInterval(() => {
    loadStatus().catch(() => {});
  }, 1500);
}

async function loadBacklight() {
  if (blDragging || blTimer) return;
  clearCdcForCmd();
  await writeLine("B?");
  const line = await readUntil((l) => /^B[= ]/.test(l.trim()) || l.trim().startsWith("B="));
  const duty = line && parseWireBl(line);
  if (duty == null) {
    log("背光读取失败（可再拖一下滑条）");
    return;
  }
  if (blDragging || blTimer || blPending != null) return;
  blDuty = duty;
  renderBl({ forceSlider: true });
  log(`背光 ${dutyToPercent(blDuty)}%（PWM ${blDuty}）`);
}

function parseWireIdle(line) {
  const m = String(line).trim().match(/^I\s*=\s*(\d+)/i);
  if (!m) return null;
  return Number(m[1]) !== 0;
}

async function loadIdleBlank() {
  const el = $("idleBlank");
  if (!el || !writer) return;
  clearCdcForCmd();
  await writeLine("I?");
  const line = await readUntil((l) => /^I=/.test(l.trim()), 1500);
  const on = line && parseWireIdle(line);
  if (on == null) {
    log("闲置息屏读取失败");
    return;
  }
  el.checked = on;
  log(on ? "闲置息屏：开（约 20s）" : "闲置息屏：关");
}

async function sendIdleBlank(on) {
  if (!writer) return;
  clearCdcForCmd();
  await writeLine(on ? "I 1" : "I 0");
  const line = await readUntil(
    (l) => l.includes("I ok") || l.includes("I err") || /^I=/.test(l.trim()),
    1500
  );
  if (line && line.includes("err")) {
    log("闲置息屏设置失败");
    return;
  }
  const reported = line && parseWireIdle(line);
  const el = $("idleBlank");
  if (el && reported != null) el.checked = reported;
  const note = line && line.includes("no peer") ? "（手持未连接，已记在接收端）" : "";
  log(`闲置息屏：${(el && el.checked) || on ? "开" : "关"}${note}`);
}

function parseWireEco(line) {
  const m = String(line).trim().match(/^E\s*=\s*(\d+)/i);
  if (!m) return null;
  return Number(m[1]) !== 0;
}

async function loadEcoRadio() {
  const el = $("ecoRadio");
  if (!el || !writer) return;
  clearCdcForCmd();
  await writeLine("E?");
  const line = await readUntil((l) => /^E=/.test(l.trim()), 1500);
  const on = line && parseWireEco(line);
  if (on == null) {
    log("射频省电读取失败");
    return;
  }
  el.checked = on;
  log(on ? "射频省电：开" : "射频省电：关");
}

async function sendEcoRadio(on) {
  if (!writer) return;
  clearCdcForCmd();
  await writeLine(on ? "E 1" : "E 0");
  const line = await readUntil(
    (l) => l.includes("E ok") || l.includes("E err") || /^E=/.test(l.trim()),
    1500
  );
  if (line && line.includes("err")) {
    log("射频省电设置失败");
    return;
  }
  const reported = line && parseWireEco(line);
  const el = $("ecoRadio");
  if (el && reported != null) el.checked = reported;
  const note = line && line.includes("no peer") ? "（手持未连接，已记在接收端）" : "";
  log(`射频省电：${(el && el.checked) || on ? "开" : "关"}${note}`);
}

async function sendBacklight(duty) {
  if (!writer) return;
  const want = Math.max(0, Math.min(255, duty | 0));
  if (blSending) {
    blPending = want;
    return;
  }
  blSending = true;
  try {
    let next = want;
    while (next != null) {
      const d = next;
      next = null;
      clearCdcForCmd();
      await writeLine(`B ${d}`);
      const line = await readUntil(
        (l) => l.includes("B ok") || l.includes("B err") || /^B=/.test(l.trim()),
        1500
      );
      if (line && line.includes("err")) {
        log("背光设置失败");
        break;
      }
      /* Prefer the value we asked for — don't snap UI to a stale reply mid-drag. */
      if (blPending != null) {
        next = blPending;
        blPending = null;
        continue;
      }
      if (blDragging || blTimer) {
        /* User moved again; keep blDuty as the live UI value. */
        break;
      }
      let reported = line && parseWireBl(line);
      if (reported == null) {
        const blLine = await readUntil((l) => /^B=/.test(l.trim()), 400);
        reported = blLine && parseWireBl(blLine);
      }
      /* Only adopt device value if it matches what we sent (or no parse). */
      if (reported == null || reported === d) {
        blDuty = d;
      } else if (Math.abs(reported - blDuty) <= 1) {
        blDuty = reported;
      }
      /* else: keep blDuty (user intent); device will catch up on next send */
      renderBl();
      const note = line && line.includes("no peer") ? "（手持未连接，已记在接收端）" : "";
      log(`背光 ${dutyToPercent(blDuty)}%${note}`);
      if (blPending != null) {
        next = blPending;
        blPending = null;
      }
    }
  } finally {
    blSending = false;
    if (blPending != null && !blDragging) {
      const q = blPending;
      blPending = null;
      sendBacklight(q).catch((e) => log(String(e)));
    }
  }
}

function scheduleBacklight(pct) {
  blDuty = percentToDuty(pct);
  /* Update label + preview only — do not rewrite range.value while dragging. */
  const label = $("blValue");
  if (label) label.textContent = `${Math.max(0, Math.min(100, Number(pct) || 0))}%`;
  const screen = $("deviceScreen");
  if (screen) screen.style.setProperty("--bl", String(Math.max(0, Math.min(1, blDuty / 255))));
  if (blTimer) clearTimeout(blTimer);
  blTimer = setTimeout(() => {
    blTimer = null;
    sendBacklight(blDuty).catch((e) => log(String(e)));
  }, 180);
}

async function openAndInit(selectedPort) {
  port = selectedPort;
  await port.open({ baudRate: 115200, bufferSize: 256 });
  reader = port.readable.getReader();
  writer = port.writable.getWriter();
  readBuf = "";
  setConnected(true);
  /* Port chooser just closed; don't leave focus on 断开. */
  focusAfterConnect();
  pumpRead();
  await new Promise((r) => setTimeout(r, 200));

  log("已连接，正在授时…");
  try {
    await syncTimeToDevice();
  } catch (e) {
    log(`授时跳过：${e.message || e}`);
  }

  log("正在读取映射…");
  try {
    clearCdcForCmd();
    await writeLine("K?");
    const line = await readUntil((l) => l.startsWith("K=") || l.startsWith("K "), 2500);
    const parsed = line && parseWireMap(line);
    if (parsed) {
      map = parsed;
      stickyMods = map[focusSlot]?.modifiers || 0;
      render();
      log(`已读取 ${line.trim()}`);
    } else {
      log("已连接，但未读到映射（可点「从设备读取」）。");
    }
  } catch (e) {
    log(`读映射失败：${e.message || e}`);
  }

  try {
    await loadBacklight();
  } catch (_) { /* optional */ }
  try {
    await loadIdleBlank();
  } catch (_) { /* optional */ }
  try {
    await loadEcoRadio();
  } catch (_) { /* optional */ }
  try {
    const st = await loadStatus();
    if (st) {
      log(`状态：${st.linked ? "手持在线" : "手持离线"} · 电量 ${st.battery}%`);
    }
  } catch (_) { /* optional */ }
  startStatusPoll();
  /* Again after async work — some browsers restore focus to the opener button. */
  focusAfterConnect();
}

function secureContextHint() {
  const origin = location.origin;
  if (location.protocol === "file:") {
    return "不能用 file://。请启动服务：./tools/serve_keymap.sh  或  python3 -m http.server 8766";
  }
  if (location.hostname === "localhost" || location.hostname === "127.0.0.1") {
    return "请用 http://localhost:8766 打开。";
  }
  /* LAN HTTP — browser blocks Web Serial unless treated as secure */
  return (
    `Web Serial 需要安全上下文，局域网纯 HTTP（${origin}）默认不可用。` +
    `可选：① ./tools/serve_keymap.sh 用 HTTPS 打开；` +
    `② Chrome 打开 chrome://flags/#unsafely-treat-insecure-origin-as-secure ，填入 ${origin} 后重启浏览器。`
  );
}

async function connect({ forcePick = false } = {}) {
  if (port && !forcePick) {
    /* Old single-button toggle — keep safe no-op; disconnect is its own button. */
    return;
  }
  if (port && forcePick) {
    await disconnect({ silent: true });
  }
  if (!window.isSecureContext) {
    log(secureContextHint());
    return;
  }
  if (!("serial" in navigator)) {
    log("需要 Chrome / Edge。若已是 HTTPS 仍无串口，请换最新 Chrome/Edge。");
    return;
  }
  try {
    const filters = [{ usbVendorId: 0x303a }]; /* Espressif / 知言 */
    let selected = null;
    if (!forcePick) {
      const known = await navigator.serial.getPorts();
      selected = known.find((p) => {
        const info = p.getInfo?.() || {};
        return info.usbVendorId === 0x303a;
      }) || null;
      if (selected) log("使用已授权的知言串口…");
    }
    if (!selected) {
      log("请选择 Zhiyan Receiver（usbmodem…）");
      selected = await navigator.serial.requestPort({ filters });
    }
    /* Dialog closed: blur opener before UI flips to 断开. */
    $("btnConnect")?.blur();
    if (document.activeElement && typeof document.activeElement.blur === "function") {
      document.activeElement.blur();
    }
    try {
      await openAndInit(selected);
    } catch (e) {
      /* Stale authorized port — ask user to pick again */
      await disconnect({ silent: true });
      if (!forcePick && e?.name !== "NotFoundError") {
        log("自动重连失败，请手动选择串口…");
        selected = await navigator.serial.requestPort({ filters });
        $("btnConnect")?.blur();
        document.activeElement?.blur?.();
        await openAndInit(selected);
      } else {
        throw e;
      }
    }
  } catch (e) {
    const name = e?.name || "";
    const msg = e?.message || String(e);
    await disconnect({ silent: true });
    if (name === "NotFoundError") {
      log("已取消选择串口。若列表为空：确认系统里已有 Zhiyan Receiver，并用 localhost 打开页面。");
    } else if (name === "NetworkError" || /busy|in use|failed to open/i.test(msg)) {
      log("串口打开失败（可能被占用）。关掉其他网页标签 / 串口监视器后重试。");
    } else {
      log(`连接失败：${name ? name + " — " : ""}${msg}`);
    }
  }
}

async function disconnect({ silent = false } = {}) {
  stopStatusPoll();
  try { reader?.releaseLock(); } catch (_) {}
  try { writer?.releaseLock(); } catch (_) {}
  try { await port?.close(); } catch (_) {}
  reader = writer = port = null;
  for (const s of KEY_ID_TO_SLOT) {
    hwDown[s] = false;
    uiDown[s] = false;
  }
  syncKeyCaps();
  lcdUnix = 0;
  lcdUnixAt = 0;
  setConnected(false);
  if (!silent) log("已断开");
}

async function loadFromDevice() {
  clearCdcForCmd();
  await writeLine("K?");
  const line = await readUntil((l) => /^K[= ]/.test(l) || l.startsWith("K="));
  const parsed = line && parseWireMap(line);
  if (!parsed) {
    log("读取失败");
    return;
  }
  map = parsed;
  render();
  await loadBacklight();
  await loadIdleBlank();
  await loadEcoRadio();
  await loadStatus();
  log(`已读取 ${line.trim()}`);
}

async function saveToDevice() {
  const cmd = `K ${bindingToWire(map.voice)},${bindingToWire(map.yes)},${bindingToWire(map.no)}`;
  clearCdcForCmd();
  await writeLine(cmd);
  const ok = await readUntil((l) => l.includes("K ok") || l.includes("K err") || l.startsWith("K="));
  if (ok && ok.includes("err")) {
    log("保存失败");
    return;
  }
  log(`已保存 ${cmd}`);
}

async function resetDevice() {
  clearCdcForCmd();
  await writeLine("K!");
  const line = await readUntil((l) => l.startsWith("K=") || l.includes("K ok"));
  await loadFromDevice();
  log(line ? `已恢复默认` : "已发送恢复默认");
}

function startRecord(slot) {
  selectSlot(slot);
  recording = slot;
  peakMods = map[slot]?.modifiers || 0;
  modsDown.clear();
  render();
  log(
    `键盘录制 ${SLOT_TITLE[slot]}：可按组合；若某键被系统抢走，请改点下方芯片。Esc 取消`
  );
}

function stopRecord() {
  recording = null;
  render();
}

function commitBinding(slot, binding) {
  map[slot] = binding;
  stickyMods = binding.modifiers;
  focusSlot = slot;
  stopRecord();
  render();
  log(`${SLOT_TITLE[slot]} → ${formatBinding(binding)} · 记得「保存到设备」`);
}

function onKeyDown(e) {
  if (!recording) return;
  if (e.code === "Escape") {
    e.preventDefault();
    stopRecord();
    log("已取消录制");
    return;
  }
  e.preventDefault();
  e.stopPropagation();

  const mods = eventMods(e);
  peakMods |= mods | (map[recording]?.modifiers || 0);

  if (isModifierCode(e.code)) {
    modsDown.add(e.code);
    /* Live-merge captured mods onto the slot so partial chords still show. */
    map[recording] = {
      modifiers: peakMods,
      keycode: map[recording]?.keycode || 0,
    };
    stickyMods = peakMods;
    render();
    return;
  }

  const hid = CODE_TO_HID[e.code];
  if (hid == null) {
    log(`不支持的键：${e.code}`);
    return;
  }
  commitBinding(recording, { modifiers: peakMods, keycode: hid });
}

function onKeyUp(e) {
  if (!recording) return;
  if (!isModifierCode(e.code)) return;
  e.preventDefault();
  modsDown.delete(e.code);
  peakMods |= eventMods(e);
  if (modsDown.size === 0 && peakMods !== 0) {
    commitBinding(recording, { modifiers: peakMods, keycode: 0 });
  }
}

function buildModChips() {
  const box = $("modChips");
  box.replaceChildren();
  for (const group of MOD_GROUPS) {
    const row = document.createElement("div");
    row.className = "mod-row";
    const lab = document.createElement("span");
    lab.className = "mod-label";
    lab.textContent = group.label;
    row.appendChild(lab);
    for (const m of group.bits) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.textContent = m.label;
      btn.dataset.mod = String(m.bit);
      btn.title = `${m.label} · 点按切换，立刻写到当前键`;
      btn.addEventListener("click", () => toggleModBit(m.bit));
      row.appendChild(btn);
    }
    box.appendChild(row);
  }
  const clearRow = document.createElement("div");
  clearRow.className = "mod-row";
  const clear = document.createElement("button");
  clear.type = "button";
  clear.className = "ghost";
  clear.textContent = "清空修饰";
  clear.title = "去掉当前键上的全部修饰键，保留普通键";
  clear.addEventListener("click", () => clearModsKeepKey());
  clearRow.appendChild(clear);
  box.appendChild(clearRow);
}

function buildPresets() {
  const box = $("presets");
  box.replaceChildren();
  for (const p of PRESETS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = p.label;
    btn.title = formatBinding(p.binding);
    btn.addEventListener("click", () => {
      const slot = focusSlot || p.slot;
      map[slot] = { ...p.binding };
      focusSlot = slot;
      stickyMods = p.binding.modifiers;
      render();
      log(`${SLOT_TITLE[slot]} ← ${p.label}（${formatBinding(p.binding)}）· 记得「保存到设备」`);
    });
    box.appendChild(btn);
  }
}

function wireUi() {
  const tip = $("bindTip");
  if (tip) {
    tip.textContent = IS_MAC
      ? "Mac：点选「右 ⌘ / 右 ⌥ / 右 Ctrl」拼组合（点一下立刻写到当前键）。系统抢走的键用点选，不要依赖「键盘录制」。可用预设「右Ctrl+右⌥」或「豆包（右Ctrl+右⌘）」。"
      : "Windows：点选「右 Ctrl / 右 Alt / 右 Win」拼组合（点一下立刻写到当前键）。右 Ctrl 被其它软件占用时网页录不到——请点芯片。可用一键预设「右Ctrl+右Alt」。";
  }

  $("btnConnect").addEventListener("click", () => connect());
  $("btnDisconnect")?.addEventListener("click", () => {
    disconnect().catch((e) => log(String(e)));
  });
  $("btnPickPort")?.addEventListener("click", () => connect({ forcePick: true }));
  $("btnLoad").addEventListener("click", () => loadFromDevice().catch((e) => log(String(e))));
  $("btnSave").addEventListener("click", () => saveToDevice().catch((e) => log(String(e))));
  $("btnReset").addEventListener("click", () => resetDevice().catch((e) => log(String(e))));
  $("btnApplyKey").addEventListener("click", () => applyStickyWithKey());
  $("btnClearKey")?.addEventListener("click", () => clearKeyKeepMods());
  $("keyInput").addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      applyStickyWithKey();
    }
  });
  const bl = $("blRange");
  bl.addEventListener("pointerdown", () => {
    blDragging = true;
  });
  bl.addEventListener("pointerup", () => {
    blDragging = false;
  });
  bl.addEventListener("pointercancel", () => {
    blDragging = false;
  });
  bl.addEventListener("input", (e) => {
    blDragging = true;
    scheduleBacklight(e.target.value);
  });
  bl.addEventListener("change", (e) => {
    if (blTimer) {
      clearTimeout(blTimer);
      blTimer = null;
    }
    blDuty = percentToDuty(e.target.value);
    blDragging = false;
    const label = $("blValue");
    if (label) label.textContent = `${dutyToPercent(blDuty)}%`;
    const screen = $("deviceScreen");
    if (screen) screen.style.setProperty("--bl", String(Math.max(0, Math.min(1, blDuty / 255))));
    sendBacklight(blDuty).catch((err) => log(String(err)));
  });
  /* If focus leaves mid-drag (rare), stop fighting the thumb. */
  bl.addEventListener("blur", () => {
    blDragging = false;
  });
  $("idleBlank")?.addEventListener("change", (e) => {
    sendIdleBlank(!!e.target.checked).catch((err) => log(String(err)));
  });
  $("ecoRadio")?.addEventListener("change", (e) => {
    sendEcoRadio(!!e.target.checked).catch((err) => log(String(err)));
  });

  document.querySelectorAll(".keycard").forEach((card) => {
    card.addEventListener("click", () => {
      selectSlot(card.dataset.slot);
    });
  });
  document.querySelectorAll(".vk-key[data-slot]").forEach((cap) => {
    const slot = cap.dataset.slot;
    const press = () => {
      selectSlot(slot);
      uiDown[slot] = true;
      if (slot === "voice") setLcdMode("talk");
      else if (slot === "yes") setLcdMode("yes");
      else if (slot === "no") setLcdMode("no");
      render();
    };
    const release = () => {
      uiDown[slot] = false;
      if (slot === "voice" && lcdMode === "talk" && !hwDown.voice) setLcdMode("idle");
      else if (slot === "yes" && lcdMode === "yes" && !hwDown.yes) setLcdMode("idle");
      else if (slot === "no" && lcdMode === "no" && !hwDown.no) setLcdMode("idle");
      syncKeyCaps();
    };
    cap.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      cap.setPointerCapture?.(e.pointerId);
      press();
      log(`按下 ${slot.toUpperCase()}`);
    });
    cap.addEventListener("pointerup", release);
    cap.addEventListener("pointercancel", release);
    cap.addEventListener("lostpointercapture", release);
  });
  document.querySelectorAll("[data-rec]").forEach((btn) => {
    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      startRecord(btn.dataset.rec);
    });
  });
  document.querySelectorAll("[data-clear]").forEach((btn) => {
    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      const slot = btn.dataset.clear;
      map[slot] = { modifiers: 0, keycode: 0 };
      if (focusSlot === slot) stickyMods = 0;
      render();
      log(`已清除 ${SLOT_TITLE[slot]}`);
    });
  });

  window.addEventListener("keydown", onKeyDown, true);
  window.addEventListener("keyup", onKeyUp, true);
}

buildModChips();
buildPresets();
wireUi();
render();
setConnected(false);
paintLcd();
lcdClockTimer = setInterval(() => {
  if (lcdMode === "idle" || lcdMode === "away") paintLcd();
}, 60_000);
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") paintLcd();
});

if (!window.isSecureContext || location.protocol === "file:") {
  log(secureContextHint());
}
