// Design tokens, text, widgets and hit areas for the NurseryPad screens.
// Everything here draws into `canvas`; nothing here touches the network or app state.
// Values are the design canvas (640x480) halved for the Core2's 320x240 panel.
#pragma once
#include <M5Unified.h>
#include "pad_types.h"

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---------------- palette ----------------
const uint16_t C_BG = rgb565(0x0e, 0x10, 0x13);
const uint16_t C_PANEL = rgb565(0x17, 0x1b, 0x20);
const uint16_t C_PANEL_HI = rgb565(0x2a, 0x32, 0x3b); // pressed
const uint16_t C_BORDER = rgb565(0x26, 0x2c, 0x33);
const uint16_t C_LINE = rgb565(0x1f, 0x25, 0x2c);
const uint16_t C_TEXT = rgb565(0xf2, 0xee, 0xe4);
const uint16_t C_MUTED = rgb565(0x9a, 0xa3, 0xad);
const uint16_t C_FAINT = rgb565(0x4a, 0x52, 0x5b);
const uint16_t C_AMBER = rgb565(0xe9, 0xa2, 0x3b);
const uint16_t C_AMBER_INK = rgb565(0x1a, 0x14, 0x08);
const uint16_t C_AMBER_BG = rgb565(0x1d, 0x1a, 0x14);
const uint16_t C_AMBER_HI = rgb565(0xf3, 0xb8, 0x5c);
const uint16_t C_TEAL = rgb565(0x3b, 0xb3, 0xa0);
const uint16_t C_TEAL_BG = rgb565(0x13, 0x1c, 0x1c);
const uint16_t C_TEAL_BORDER = rgb565(0x24, 0x5c, 0x55);
const uint16_t C_PURPLE = rgb565(0xb4, 0x8c, 0xe8);
const uint16_t C_PURPLE_INK = rgb565(0x17, 0x0f, 0x24);
const uint16_t C_PURPLE_BG = rgb565(0x1a, 0x16, 0x22);
const uint16_t C_PURPLE_HI = rgb565(0xc8, 0xa8, 0xf2);
const uint16_t C_BLUE = rgb565(0x7f, 0x9c, 0xf5);
const uint16_t C_BLUE_INK = rgb565(0x0d, 0x13, 0x30);
const uint16_t C_BLUE_BG = rgb565(0x14, 0x18, 0x26);
const uint16_t C_BLUE_HI = rgb565(0x9f, 0xb6, 0xf8);
const uint16_t C_GREEN_BG = rgb565(0x10, 0x22, 0x1a);
const uint16_t C_GREEN_BORDER = rgb565(0x1f, 0x8a, 0x45);
const uint16_t C_GREEN_DOT = rgb565(0x3d, 0xdc, 0x6d);
const uint16_t C_RED = rgb565(0xe0, 0x52, 0x4a);
const uint16_t C_TOAST = rgb565(0xf2, 0xee, 0xe4);
const uint16_t C_TOAST_INK = rgb565(0x0e, 0x10, 0x13);
const uint16_t C_UNDO = rgb565(0x8a, 0x4b, 0x00);
const uint16_t C_WHITE = rgb565(0xff, 0xff, 0xff);
const uint16_t C_BLACK = rgb565(0, 0, 0);
// crib states (dashboard backgrounds and the crib circle)
const uint16_t C_ASLEEP = rgb565(0x1f, 0x8a, 0x45);
const uint16_t C_SETTLING = rgb565(0xc9, 0xa2, 0x27);
const uint16_t C_AWAKE = rgb565(0xd9, 0x74, 0x1c);
const uint16_t C_CRYING = rgb565(0xc3, 0x27, 0x2b);
const uint16_t C_AWAY = rgb565(0x3f, 0x4d, 0x61);
const uint16_t C_YELLOW_INK = rgb565(0x21, 0x19, 0x00);
// lamp / night (from CradleWatch)
const uint16_t C_NIGHT = rgb565(46, 143, 94);
const uint16_t C_NIGHTDOT = rgb565(53, 199, 127);

// ---------------- type scale (DejaVu bitmap faces bundled with M5GFX) ----------------
#define F_SMALL (&fonts::DejaVu12) // captions, subtitles, clock
#define F_BODY (&fonts::DejaVu18)  // titles, button labels, tile labels
#define F_BIG (&fonts::DejaVu24)   // LEFT / RIGHT, START, stat values
#define F_HERO (&fonts::DejaVu40)  // dashboard state word, feed summary time
#define F_TIMER (&fonts::DejaVu56) // running timers

// ---------------- layout ----------------
const int SCR_W = 320, SCR_H = 240;
const int BAR_H = 36;   // top bar
const int STRIP_H = 36; // timer strip
const int PAD = 8;      // screen edge padding
const int GAP = 6;      // between tiles
const int R_TILE = 9;   // tile corner radius
const int R_BTN = 7;    // button corner radius
const int MIN_TAP = 44; // never register a smaller hit area

M5Canvas canvas(&M5.Display);

uint16_t lerpCol(uint16_t a, uint16_t b, float t) {
  if (t <= 0.0f)
    return a;
  if (t >= 1.0f)
    return b;
  int ar = a >> 11, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = b >> 11, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  return (uint16_t)(((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) |
                    (int)(ab + (bb - ab) * t));
}

// ---------------- text ----------------
// Glyphs are drawn transparently; `bg` names the colour under the text so a switch to an
// anti-aliased face later needs no call-site changes.
void text(const String &s, int x, int y, const lgfx::IFont *font, uint16_t fg, uint16_t bg,
          lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  (void)bg;
  canvas.setFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(fg);
  canvas.drawString(s, x, y);
}
int textW(const String &s, const lgfx::IFont *font) {
  canvas.setFont(font);
  return canvas.textWidth(s);
}
/** Shortens `s` so it fits `maxW`, ending in ".." when cut. */
String fit(const String &s, const lgfx::IFont *font, int maxW) {
  if (textW(s, font) <= maxW)
    return s;
  String out = s;
  while (out.length() > 1) {
    out.remove(out.length() - 1);
    if (textW(out + "..", font) <= maxW)
      return out + "..";
  }
  return out;
}

// ---------------- hit areas ----------------
// Every tappable thing registers itself while it is drawn; the touch handler looks the
// press up here. Areas are grown to MIN_TAP and given 4 px of slop.
struct Hit {
  int x, y, w, h, id;
  bool enabled;
};
Hit hits[24];
int hitCount = 0;
int pressedId = 0; // while a finger is down on a hit area
void hit(int x, int y, int w, int h, int id, bool enabled = true) {
  if (hitCount >= 24)
    return;
  if (w < MIN_TAP) {
    x -= (MIN_TAP - w) / 2;
    w = MIN_TAP;
  }
  if (h < MIN_TAP) {
    y -= (MIN_TAP - h) / 2;
    h = MIN_TAP;
  }
  hits[hitCount++] = {x, y, w, h, id, enabled};
}
int hitAt(int px, int py, int slop = 4) {
  for (int i = hitCount - 1; i >= 0; i--) {
    const Hit &h = hits[i];
    if (px >= h.x - slop && px < h.x + h.w + slop && py >= h.y - slop && py < h.y + h.h + slop)
      return h.enabled ? h.id : -1; // -1: a disabled control was hit, swallow the tap
  }
  return 0;
}
bool hitDown(int id) { return pressedId == id; }

// ---------------- widgets ----------------
void panel(int x, int y, int w, int h, uint16_t bg, uint16_t border = 0, int r = R_TILE, int stroke = 1) {
  canvas.fillRoundRect(x, y, w, h, r, bg);
  if (border)
    for (int i = 0; i < stroke; i++)
      canvas.drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, r - i, border);
}
/** A tile registers its hit area and lightens while pressed. Returns the fill used so
 *  callers can blend text against it. */
uint16_t tile(int x, int y, int w, int h, int id, uint16_t bg, uint16_t border = C_BORDER, int stroke = 1,
              bool enabled = true) {
  uint16_t fill = !enabled ? C_BG : hitDown(id) ? lerpCol(bg, C_WHITE, 0.10f) : bg;
  panel(x, y, w, h, fill, enabled ? border : C_LINE, R_TILE, stroke);
  hit(x, y, w, h, id, enabled);
  return fill;
}
/** Filled accent button (STOP, SAVE). */
void primaryButton(int x, int y, int w, int h, int id, const String &label, uint16_t accent, uint16_t ink,
                   const lgfx::IFont *font = F_BODY, bool enabled = true) {
  uint16_t fill = !enabled ? C_PANEL : hitDown(id) ? lerpCol(accent, C_WHITE, 0.18f) : accent;
  panel(x, y, w, h, fill, enabled ? 0 : C_LINE, R_TILE);
  text(label, x + w / 2, y + h / 2, font, enabled ? ink : C_FAINT, fill);
  hit(x, y, w, h, id, enabled);
}
/** Quiet panel button (Discard, Skip, Switch, End now). */
void quietButton(int x, int y, int w, int h, int id, const String &label, uint16_t fg = C_TEXT,
                 const lgfx::IFont *font = F_BODY, bool enabled = true) {
  uint16_t fill = tile(x, y, w, h, id, C_PANEL, C_BORDER, 1, enabled);
  text(label, x + w / 2, y + h / 2, font, enabled ? fg : C_FAINT, fill);
}
/** Two-way segmented choice; `first` selects the left option. */
void segmented(int x, int y, int w, int h, int idA, int idB, const String &a, const String &b, bool first) {
  int half = (w - GAP) / 2;
  uint16_t selBg = C_TOAST, selFg = C_TOAST_INK;
  uint16_t fa = tile(x, y, half, h, idA, first ? selBg : C_PANEL, first ? selBg : C_BORDER);
  text(a, x + half / 2, y + h / 2, F_SMALL, first ? selFg : C_MUTED, fa);
  uint16_t fb = tile(x + half + GAP, y, half, h, idB, !first ? selBg : C_PANEL, !first ? selBg : C_BORDER);
  text(b, x + half + GAP + half / 2, y + h / 2, F_SMALL, !first ? selFg : C_MUTED, fb);
}

// ---------------- icons (stroke drawings at roughly 18 px) ----------------
void thickLine(int x0, int y0, int x1, int y1, uint16_t c) {
  canvas.drawLine(x0, y0, x1, y1, c);
  canvas.drawLine(x0 + 1, y0, x1 + 1, y1, c);
  canvas.drawLine(x0, y0 + 1, x1, y1 + 1, c);
}
void iconChevronLeft(int cx, int cy, uint16_t c) {
  thickLine(cx + 4, cy - 7, cx - 3, cy, c);
  thickLine(cx - 3, cy, cx + 4, cy + 7, c);
}
void iconChevronRight(int cx, int cy, uint16_t c) {
  thickLine(cx - 4, cy - 6, cx + 2, cy, c);
  thickLine(cx + 2, cy, cx - 4, cy + 6, c);
}
void iconChevronDown(int cx, int cy, uint16_t c) {
  thickLine(cx - 5, cy - 2, cx, cy + 3, c);
  thickLine(cx, cy + 3, cx + 5, cy - 2, c);
}
void iconDrop(int cx, int cy, uint16_t c) { // feed
  canvas.fillCircle(cx, cy + 3, 7, c);
  canvas.fillTriangle(cx - 7, cy + 2, cx + 7, cy + 2, cx, cy - 9, c);
}
void iconSwap(int cx, int cy, uint16_t c) { // change
  thickLine(cx - 9, cy - 4, cx + 8, cy - 4, c);
  thickLine(cx + 8, cy - 4, cx + 3, cy - 9, c);
  thickLine(cx + 9, cy + 4, cx - 8, cy + 4, c);
  thickLine(cx - 8, cy + 4, cx - 3, cy + 9, c);
}
void iconPump(int cx, int cy, uint16_t c) { // pump
  canvas.drawRoundRect(cx - 6, cy - 9, 13, 19, 3, c);
  canvas.drawRoundRect(cx - 5, cy - 8, 11, 17, 2, c);
  canvas.drawFastHLine(cx - 6, cy - 2, 13, c);
  canvas.drawFastHLine(cx - 6, cy + 3, 13, c);
}
void iconMoon(int cx, int cy, uint16_t c, uint16_t bg) { // sleep
  canvas.fillCircle(cx, cy, 9, c);
  canvas.fillCircle(cx + 5, cy - 4, 8, bg);
}
void iconPlay(int cx, int cy, uint16_t c) { canvas.fillTriangle(cx - 5, cy - 7, cx - 5, cy + 7, cx + 7, cy, c); }
void iconMinus(int cx, int cy, uint16_t c) { canvas.fillRect(cx - 9, cy - 1, 18, 3, c); }
void iconPlus(int cx, int cy, uint16_t c) {
  canvas.fillRect(cx - 9, cy - 1, 18, 3, c);
  canvas.fillRect(cx - 1, cy - 9, 3, 18, c);
}
void iconHome(int cx, int cy, uint16_t c) {
  canvas.fillTriangle(cx - 7, cy - 1, cx + 7, cy - 1, cx, cy - 7, c);
  canvas.fillRect(cx - 5, cy - 1, 11, 7, c);
}
// level 0 = muted (X); 1-3 = that many waves
void iconSpeaker(int x, int y, uint16_t col, int level) {
  canvas.fillRect(x - 9, y - 4, 5, 9, col);
  canvas.fillTriangle(x - 5, y, x + 2, y - 8, x + 2, y + 8, col);
  if (level <= 0) {
    for (int o = 0; o < 2; ++o) {
      canvas.drawLine(x + 6, y - 6 + o, x + 14, y + 2 + o, col);
      canvas.drawLine(x + 14, y - 6 + o, x + 6, y + 2 + o, col);
    }
    return;
  }
  for (int w = 0; w < level && w < 3; ++w) {
    int r = 8 + w * 4;
    canvas.fillArc(x + 2, y, r, r - 2, 300, 360, col);
    canvas.fillArc(x + 2, y, r, r - 2, 0, 60, col);
  }
}
/** The crib-state circle: filled dot with a dark gap and a thin ring, as on the canvas. */
void cribCircle(int cx, int cy, uint16_t fill, uint16_t ring, uint16_t gap, bool hollow = false) {
  canvas.fillCircle(cx, cy, 13, ring);
  canvas.fillCircle(cx, cy, 12, gap);
  if (hollow) {
    canvas.fillCircle(cx, cy, 10, fill);
    canvas.fillCircle(cx, cy, 7, gap);
  } else
    canvas.fillCircle(cx, cy, 10, fill);
}

// ---------------- formatting ----------------
String two(int n) { return (n < 10 ? "0" : "") + String(n); }
/** 12:34 or 1:02:03 for running timers. */
String fmtTimer(long secs) {
  if (secs < 0)
    secs = 0;
  if (secs < 3600)
    return two(secs / 60) + ":" + two(secs % 60);
  return String(secs / 3600) + ":" + two((secs % 3600) / 60) + ":" + two(secs % 60);
}
/** 45m, 2h 10m, 1d 3h for durations and "ago" strings. */
String fmtDur(long secs) {
  if (secs < 0)
    return "";
  if (secs < 60)
    return String(secs) + "s";
  if (secs < 3600)
    return String(secs / 60) + "m";
  if (secs < 86400) {
    long m = (secs % 3600) / 60;
    return String(secs / 3600) + "h" + (m ? " " + String(m) + "m" : "");
  }
  return String(secs / 86400) + "d " + String((secs % 86400) / 3600) + "h";
}
String fmtAgo(long secs) {
  if (secs < 0)
    return "";
  if (secs < 60)
    return "just now";
  return fmtDur(secs) + " ago";
}
/** 24-hour wall clock in the board's zone. */
String clockStr(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return two(lt.tm_hour) + ":" + two(lt.tm_min);
}
