// Design tokens, text, widgets and hit areas for the Cradlewatch pad screens.
// Everything here draws into `canvas`; nothing here touches the network or app state.
//
// The look is 16-bit console menu: a limited palette, hard square edges, a light bevel on the
// top-left of every raised surface and a dark one on the bottom-right, a hard offset shadow, and
// bitmap type on an 8 px grid. Nothing is anti-aliased and nothing is rounded. The palette comes
// from the 2026-09-19 pixel design package; its woodland scenery deliberately does not.
#pragma once
#include <M5Unified.h>
#include "pad_types.h"
#include "pixel_art.h"

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---------------- palette ----------------
// Named for the role, not the hue, so a screen never picks a colour by eye.
const uint16_t C_BG = rgb565(0x14, 0x26, 0x1e);      // deep green field
const uint16_t C_BG_DOT = rgb565(0x1b, 0x2f, 0x25);  // the field's quiet stipple
const uint16_t C_PANEL = rgb565(0x19, 0x2b, 0x40);   // midnight blue menu box
const uint16_t C_PANEL_HI = rgb565(0x2c, 0x44, 0x5e);// the same box, pressed
const uint16_t C_BORDER = rgb565(0xa9, 0x94, 0x57);  // antique gold frame
const uint16_t C_BEVEL_HI = rgb565(0xd4, 0xbf, 0x78);// frame bevel, top-left
const uint16_t C_BEVEL_LO = rgb565(0x46, 0x4a, 0x38);// frame bevel, bottom-right
const uint16_t C_SHADOW = rgb565(0x08, 0x12, 0x10);  // hard drop shadow
const uint16_t C_LINE = rgb565(0x34, 0x43, 0x39);    // hairlines and rules
const uint16_t C_TEXT = rgb565(0xee, 0xe9, 0xcd);    // warm cream
const uint16_t C_MUTED = rgb565(0xa5, 0xb6, 0xa2);   // secondary text
const uint16_t C_FAINT = rgb565(0x5b, 0x5e, 0x42);   // disabled
const uint16_t C_AMBER = rgb565(0xe7, 0xc6, 0x7a);   // antique gold text and accents
const uint16_t C_AMBER_INK = rgb565(0x1a, 0x14, 0x08);
const uint16_t C_AMBER_BG = rgb565(0x3a, 0x2e, 0x16);
const uint16_t C_AMBER_HI = rgb565(0xf3, 0xd6, 0x81);
const uint16_t C_TEAL = rgb565(0x91, 0xbb, 0xc6);  // water blue
const uint16_t C_TEAL_BG = rgb565(0x1c, 0x38, 0x42);
const uint16_t C_TEAL_BORDER = rgb565(0x41, 0x6d, 0x82);
const uint16_t C_PURPLE = rgb565(0xd9, 0xa2, 0x9a); // rose, standing in for the old violet
const uint16_t C_PURPLE_INK = rgb565(0x24, 0x14, 0x12);
const uint16_t C_PURPLE_BG = rgb565(0x45, 0x2a, 0x28);
const uint16_t C_PURPLE_HI = rgb565(0xef, 0xc2, 0xb9);
const uint16_t C_BLUE = rgb565(0x91, 0xbb, 0xc6);
const uint16_t C_BLUE_INK = rgb565(0x10, 0x1d, 0x2c);
const uint16_t C_BLUE_BG = rgb565(0x1a, 0x2c, 0x42);
const uint16_t C_BLUE_HI = rgb565(0xb6, 0xd6, 0xde);
const uint16_t C_GREEN_BG = rgb565(0x1c, 0x38, 0x26);
const uint16_t C_GREEN_BORDER = rgb565(0x65, 0x8c, 0x59);
const uint16_t C_GREEN_DOT = rgb565(0x9e, 0xcf, 0x7e);
const uint16_t C_RED = rgb565(0xbd, 0x78, 0x65);   // terracotta
const uint16_t C_TOAST = rgb565(0xee, 0xe9, 0xcd);
const uint16_t C_TOAST_INK = rgb565(0x14, 0x26, 0x1e);
const uint16_t C_UNDO = rgb565(0x8a, 0x4b, 0x00);
const uint16_t C_WHITE = rgb565(0xff, 0xff, 0xff);
const uint16_t C_BLACK = rgb565(0, 0, 0);
// crib states: each also carries an explicit word on screen, never colour alone
const uint16_t C_ASLEEP = rgb565(0x65, 0x8c, 0x59);   // moss green
const uint16_t C_SETTLING = rgb565(0xbd, 0xa1, 0x74); // dry gold
const uint16_t C_AWAKE = rgb565(0xe7, 0xc6, 0x7a);    // antique gold
const uint16_t C_CRYING = rgb565(0xbd, 0x78, 0x65);   // terracotta
const uint16_t C_AWAY = rgb565(0x41, 0x6d, 0x82);     // deep water
const uint16_t C_YELLOW_INK = rgb565(0x21, 0x19, 0x00);
const uint16_t C_NIGHT = rgb565(0x65, 0x8c, 0x59);
const uint16_t C_NIGHTDOT = rgb565(0x9e, 0xcf, 0x7e);

// The pixel faces are ASCII only, so a typographic separator has to be ASCII too.
#define SEP "  -  "

// ---------------- type ----------------
// Two bitmap faces on one 8 px grid, used only at integer scales so glyphs stay on the pixel
// grid. Size travels with the face: a caller names a role, never a size.
struct PixFont {
  const lgfx::IFont *font;
  uint8_t size;
};
const PixFont F_SMALL{&fonts::Font8x8C64, 1};    //  8 x  8  captions, sublabels, the clock
const PixFont F_BODY{&fonts::AsciiFont8x16, 1};  //  8 x 16  titles, button labels
const PixFont F_BIG{&fonts::AsciiFont8x16, 2};   // 16 x 32  LEFT / RIGHT, stat values
const PixFont F_HERO{&fonts::Font8x8C64, 4};     // 32 x 32  the dashboard state word
const PixFont F_TIMER{&fonts::AsciiFont8x16, 3}; // 24 x 48  running timers

// ---------------- layout ----------------
const int SCR_W = 320, SCR_H = 240;
const int BAR_H = 44;   // top bar; MIN_TAP, so Back and the crib gem are full-size targets
const int STRIP_H = 36; // timer strip
const int PAD = 8;      // screen edge padding
const int GAP = 6;      // between tiles
const int R_TILE = 0;   // square corners: kept so call sites need not change
const int R_BTN = 0;
const int MIN_TAP = 44; // never register a smaller hit area
const int BEVEL = 2;    // frame and bevel thickness, in real pixels

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
// Glyphs are drawn transparently; `bg` names the colour under the text, kept so a call site can
// blend against a tile's fill without knowing it.
void text(const String &s, int x, int y, const PixFont &f, uint16_t fg, uint16_t bg,
          lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  (void)bg;
  canvas.setFont(f.font);
  canvas.setTextSize(f.size);
  canvas.setTextDatum(datum);
  canvas.setTextColor(fg);
  canvas.drawString(s, x, y);
  canvas.setTextSize(1);
}
int textW(const String &s, const PixFont &f) {
  canvas.setFont(f.font);
  canvas.setTextSize(f.size);
  int w = canvas.textWidth(s);
  canvas.setTextSize(1);
  return w;
}
/** Shortens `s` so it fits `maxW`, ending in ".." when cut. */
String fit(const String &s, const PixFont &f, int maxW) {
  if (textW(s, f) <= maxW)
    return s;
  String out = s;
  while (out.length() > 1) {
    out.remove(out.length() - 1);
    if (textW(out + "..", f) <= maxW)
      return out + "..";
  }
  return out;
}

// ---------------- sprites ----------------
/** Blits pixel artwork at an integer scale. Palette index 0 is transparent. */
void sprite(const Sprite &s, int x, int y, int scale = 1) {
  for (int row = 0; row < s.h; row++)
    for (int col = 0; col < s.w; col++) {
      uint8_t i = s.px[row * s.w + col];
      if (!i)
        continue;
      if (scale == 1)
        canvas.drawPixel(x + col, y + row, SPR_PAL[i]);
      else
        canvas.fillRect(x + col * scale, y + row * scale, scale, scale, SPR_PAL[i]);
    }
}
/** Blits artwork centred on a point. */
void spriteAt(const Sprite &s, int cx, int cy, int scale = 1) {
  sprite(s, cx - s.w * scale / 2, cy - s.h * scale / 2, scale);
}

// ---------------- hit areas ----------------
// Every tappable thing registers itself while it is drawn, keeping two rectangles: the one it
// actually drew, and that rectangle grown to MIN_TAP and confined to the band it belongs to.
// A press inside a drawn rectangle wins outright; a near miss goes to the nearest centre.
// Resolving near misses by distance rather than by registration order is what stops the body's
// first control from stealing the top bar, which the bar can never win on order because it draws
// first. Confining growth to a band is what stops a short control sitting flush under the bar
// from growing up over the Back chevron.
struct Hit {
  int16_t x, y, w, h;     // grown to MIN_TAP, clamped to the band: used for near misses
  int16_t dx, dy, dw, dh; // exactly as drawn: used for direct hits and for repainting
  int16_t id;
  bool enabled;
};
Hit hits[24];
int hitCount = 0;
int pressedId = 0; // the control under the finger, or 0; drives the pressed highlight
int hitBandTop = 0, hitBandBottom = SCR_H;
/** Limits where the controls drawn next may grow to. The bar and the strip own their rows. */
void hitBand(int top, int bottom) {
  hitBandTop = top;
  hitBandBottom = bottom;
}
void hit(int x, int y, int w, int h, int id, bool enabled = true) {
  if (hitCount >= 24)
    return;
  int gx = x, gy = y, gw = w, gh = h;
  if (gw < MIN_TAP) {
    gx -= (MIN_TAP - gw) / 2;
    gw = MIN_TAP;
  }
  if (gh < MIN_TAP) {
    gy -= (MIN_TAP - gh) / 2;
    gh = MIN_TAP;
  }
  // Slide back inside the band before giving any height up, so a short control grows the only
  // way it can rather than reaching into its neighbour's rows.
  if (gy + gh > hitBandBottom)
    gy = hitBandBottom - gh;
  if (gy < hitBandTop)
    gy = hitBandTop;
  if (gy + gh > hitBandBottom)
    gh = hitBandBottom - gy;
  if (gx + gw > SCR_W)
    gx = SCR_W - gw;
  if (gx < 0)
    gx = 0;
  if (gx + gw > SCR_W)
    gw = SCR_W - gx;
  if (gh <= 0 || gw <= 0)
    return;
  hits[hitCount++] = {(int16_t)gx, (int16_t)gy, (int16_t)gw, (int16_t)gh,
                      (int16_t)x,  (int16_t)y,  (int16_t)w,  (int16_t)h,
                      (int16_t)id, enabled};
}
/** Resolves a touch: 0 for nothing, -1 when a disabled control owns the point. */
int hitAt(int px, int py, int slop = 4) {
  if (py >= SCR_H) // below the glass: those rows are buttons A/B/C, never a screen control
    return 0;
  for (int i = hitCount - 1; i >= 0; i--) { // inside something drawn: topmost wins
    const Hit &h = hits[i];
    if (px >= h.dx && px < h.dx + h.dw && py >= h.dy && py < h.dy + h.dh)
      return h.enabled ? h.id : -1;
  }
  int best = 0;
  long bestDist = 0;
  bool found = false;
  for (int i = hitCount - 1; i >= 0; i--) { // near miss: nearest centre wins
    const Hit &h = hits[i];
    if (px < h.x - slop || px >= h.x + h.w + slop || py < h.y - slop || py >= h.y + h.h + slop)
      continue;
    long ddx = px - (h.x + h.w / 2), ddy = py - (h.y + h.h / 2);
    long dist = ddx * ddx + ddy * ddy;
    if (!found || dist < bestDist) {
      found = true;
      bestDist = dist;
      best = h.enabled ? h.id : -1;
    }
  }
  return best;
}
/** The rectangle a control drew, so just that control can be repainted. */
bool hitRect(int id, int &x, int &y, int &w, int &h) {
  for (int i = hitCount - 1; i >= 0; i--)
    if (hits[i].id == id) {
      x = hits[i].dx;
      y = hits[i].dy;
      w = hits[i].dw;
      h = hits[i].dh;
      return true;
    }
  return false;
}
/** True while the finger is down on this control and has not rolled off it. */
bool hitDown(int id) { return pressedId == id; }

// ---------------- widgets ----------------
/** The field behind every screen: flat, with a quiet stipple so it reads as tiled rather than
 *  blank. Cheap enough to redraw inside a dirty rectangle. */
void backdrop(int x, int y, int w, int h) {
  canvas.fillRect(x, y, w, h, C_BG);
  for (int py = (y + 7) & ~7; py < y + h; py += 8)
    for (int px = (x + 7) & ~7; px < x + w; px += 8)
      canvas.drawPixel(px, py, C_BG_DOT);
}
/** A 16-bit menu box: hard offset shadow, a frame, then a bevel that is light on the top-left and
 *  dark on the bottom-right. `sunken` swaps the bevel, which is how a pressed control reads. */
void pixelBox(int x, int y, int w, int h, uint16_t fill, uint16_t frame, bool sunken = false,
              bool shadow = true) {
  if (w < 2 * BEVEL || h < 2 * BEVEL)
    return;
  if (shadow)
    canvas.fillRect(x + BEVEL, y + BEVEL, w, h, C_SHADOW);
  canvas.fillRect(x, y, w, h, frame);
  canvas.fillRect(x + BEVEL, y + BEVEL, w - 2 * BEVEL, h - 2 * BEVEL, fill);
  uint16_t hi = sunken ? C_BEVEL_LO : C_BEVEL_HI, lo = sunken ? C_BEVEL_HI : C_BEVEL_LO;
  for (int i = 0; i < BEVEL; i++) {
    canvas.drawFastHLine(x + i, y + i, w - 2 * i, hi);
    canvas.drawFastVLine(x + i, y + i, h - 2 * i, hi);
    canvas.drawFastHLine(x + i, y + h - 1 - i, w - 2 * i, lo);
    canvas.drawFastVLine(x + w - 1 - i, y + i, h - 2 * i, lo);
  }
}
/** Kept for call sites that want a plain framed panel. The radius argument is ignored: this
 *  interface has square corners everywhere. */
void panel(int x, int y, int w, int h, uint16_t bg, uint16_t border = 0, int r = R_TILE, int stroke = 1) {
  (void)r;
  (void)stroke;
  pixelBox(x, y, w, h, bg, border ? border : C_LINE);
}
/** A tile registers its hit area, and on press sinks its bevel and takes a gold frame. Returns
 *  the fill used so callers can blend text against it. */
uint16_t tile(int x, int y, int w, int h, int id, uint16_t bg, uint16_t border = C_BORDER, int stroke = 1,
              bool enabled = true) {
  (void)stroke;
  bool down = enabled && hitDown(id);
  uint16_t fill = !enabled ? C_BG : down ? lerpCol(bg, C_BEVEL_HI, 0.22f) : bg;
  uint16_t frame = !enabled ? C_LINE : down ? C_AMBER_HI : border;
  pixelBox(x, y, w, h, fill, frame, down, !down);
  hit(x, y, w, h, id, enabled);
  return fill;
}
/** Filled accent button (STOP, SAVE): the accent is the frame and the face is a dark wash of it,
 *  so the label stays cream and legible instead of sitting on a bright field. */
void primaryButton(int x, int y, int w, int h, int id, const String &label, uint16_t accent, uint16_t ink,
                   const PixFont &font = F_BODY, bool enabled = true) {
  (void)ink;
  bool down = enabled && hitDown(id);
  uint16_t face = !enabled ? C_BG : lerpCol(accent, C_SHADOW, down ? 0.55f : 0.72f);
  pixelBox(x, y, w, h, face, enabled ? (down ? C_AMBER_HI : accent) : C_LINE, down, !down);
  text(label, x + w / 2, y + h / 2, font, enabled ? C_TEXT : C_FAINT, face);
  hit(x, y, w, h, id, enabled);
}
/** Quiet panel button (Discard, Skip, Switch, End now). */
void quietButton(int x, int y, int w, int h, int id, const String &label, uint16_t fg = C_TEXT,
                 const PixFont &font = F_BODY, bool enabled = true) {
  uint16_t fill = tile(x, y, w, h, id, C_PANEL, C_BORDER, 1, enabled);
  text(label, x + w / 2, y + h / 2, font, enabled ? fg : C_FAINT, fill);
}
/** Two-way segmented choice; `first` selects the left option. The chosen side is the lit one. */
void segmented(int x, int y, int w, int h, int idA, int idB, const String &a, const String &b, bool first) {
  int half = (w - GAP) / 2;
  uint16_t fa = tile(x, y, half, h, idA, first ? C_AMBER_BG : C_PANEL, first ? C_AMBER : C_LINE);
  text(a, x + half / 2, y + h / 2, F_SMALL, first ? C_AMBER : C_MUTED, fa);
  uint16_t fb = tile(x + half + GAP, y, half, h, idB, !first ? C_AMBER_BG : C_PANEL, !first ? C_AMBER : C_LINE);
  text(b, x + half + GAP + half / 2, y + h / 2, F_SMALL, !first ? C_AMBER : C_MUTED, fb);
}

// ---------------- glyphs ----------------
// Chevrons, plus and minus are interface furniture rather than artwork, so they stay procedural.
// They are drawn from whole blocks, never lines, so they sit on the pixel grid like the sprites.
void blk(int x, int y, int w, int h, uint16_t c) { canvas.fillRect(x, y, w, h, c); }
void iconChevronLeft(int cx, int cy, uint16_t c) {
  for (int i = 0; i < 4; i++) {
    blk(cx - 4 + i * 2, cy - 2 - i * 2, 2, 2, c);
    blk(cx - 4 + i * 2, cy + i * 2, 2, 2, c);
  }
  blk(cx - 4, cy - 2, 2, 4, c);
}
void iconChevronRight(int cx, int cy, uint16_t c) {
  for (int i = 0; i < 4; i++) {
    blk(cx + 2 - i * 2, cy - 2 - i * 2, 2, 2, c);
    blk(cx + 2 - i * 2, cy + i * 2, 2, 2, c);
  }
  blk(cx + 2, cy - 2, 2, 4, c);
}
void iconChevronDown(int cx, int cy, uint16_t c) {
  for (int i = 0; i < 3; i++) {
    blk(cx - 6 + i * 2, cy - 2 + i * 2, 2, 2, c);
    blk(cx + 4 - i * 2, cy - 2 + i * 2, 2, 2, c);
  }
}
void iconMinus(int cx, int cy, uint16_t c) { blk(cx - 10, cy - 2, 20, 4, c); }
void iconPlus(int cx, int cy, uint16_t c) {
  blk(cx - 10, cy - 2, 20, 4, c);
  blk(cx - 2, cy - 10, 4, 20, c);
}
void iconPlay(int cx, int cy, uint16_t c) {
  for (int i = 0; i < 6; i++)
    blk(cx - 6 + i * 2, cy - 10 + i * 2, 2, 20 - i * 4, c);
}
/** Volume, drawn rather than blitted because the wave count carries the level. */
void iconSpeaker(int x, int y, uint16_t col, int level) {
  blk(x - 10, y - 3, 4, 6, col);
  for (int i = 0; i < 4; i++)
    blk(x - 6 + i, y - 3 - i * 2, 2, 6 + i * 4, col);
  if (level <= 0) {
    for (int i = 0; i < 4; i++) {
      blk(x + 4 + i * 2, y - 6 + i * 2, 2, 2, col);
      blk(x + 10 - i * 2, y - 6 + i * 2, 2, 2, col);
    }
    return;
  }
  for (int w = 0; w < level && w < 3; ++w)
    blk(x + 4 + w * 4, y - 3 - w * 3, 2, 6 + w * 6, col);
}
/** The crib-state indicator: a bevelled gem rather than a circle, so it belongs to the menus.
 *  `hollow` is the unknown state, which reads as an empty setting. */
void stateGem(int cx, int cy, uint16_t fill, uint16_t frame, uint16_t gap, bool hollow = false) {
  const int r = 11;
  canvas.fillRect(cx - r, cy - r, r * 2, r * 2, frame);
  canvas.fillRect(cx - r + 2, cy - r + 2, r * 2 - 4, r * 2 - 4, gap);
  if (hollow) {
    canvas.fillRect(cx - r + 3, cy - r + 3, r * 2 - 6, r * 2 - 6, fill);
    canvas.fillRect(cx - r + 5, cy - r + 5, r * 2 - 10, r * 2 - 10, gap);
  } else {
    canvas.fillRect(cx - r + 3, cy - r + 3, r * 2 - 6, r * 2 - 6, fill);
    canvas.drawFastHLine(cx - r + 3, cy - r + 3, r * 2 - 6, lerpCol(fill, C_WHITE, 0.35f));
    canvas.drawFastVLine(cx - r + 3, cy - r + 3, r * 2 - 6, lerpCol(fill, C_WHITE, 0.35f));
    canvas.drawFastHLine(cx - r + 3, cy + r - 4, r * 2 - 6, lerpCol(fill, C_SHADOW, 0.35f));
    canvas.drawFastVLine(cx + r - 4, cy - r + 3, r * 2 - 6, lerpCol(fill, C_SHADOW, 0.35f));
  }
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
