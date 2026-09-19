// Adapted from CradleWatch: palette, sounds, haptics and renderers. No network calls.
#pragma once
// ---------------- 16-colour Hyrule-inspired pixel palette (RGB565) ----------------
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t COL_GREEN = rgb565(40, 128, 72);    // Kokiri tunic
const uint16_t COL_YELLOW = rgb565(248, 184, 40);  // Triforce gold
const uint16_t COL_ORANGE = rgb565(216, 112, 32);  // torch
const uint16_t COL_RED = rgb565(184, 48, 48);      // heart
const uint16_t COL_AWAYBG = rgb565(48, 64, 96);    // Dark World blue
const uint16_t COL_AWAYTX = rgb565(152, 200, 208); // Zora ice
const uint16_t COL_DARK = rgb565(24, 24, 40);      // dungeon night
const uint16_t COL_PANEL = rgb565(48, 48, 64);     // stone tile
const uint16_t COL_DIM = rgb565(168, 160, 136);    // weathered parchment
const uint16_t COL_FAINT = rgb565(80, 72, 88);
const uint16_t COL_WHITE = rgb565(248, 232, 200);  // fairy-light cream
const uint16_t COL_YELTX = rgb565(48, 32, 16);
const uint16_t COL_AMBER = rgb565(248, 200, 80);
const uint16_t COL_BANNER = rgb565(64, 40, 56);
const uint16_t COL_NIGHT = rgb565(72, 152, 104);
const uint16_t COL_NIGHTDOT = rgb565(128, 216, 144);
const uint16_t COL_BLACK = rgb565(0, 0, 0);

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

const uint32_t SETTLED_SECS = 10 * 60;    // sleeping this long -> green
const uint32_t POLL_ACTIVE_MS = 30000;    // status poll while things happen (API floor)
const uint32_t POLL_CALM_MS = 60000;      // status poll during settled sleep / away
const uint32_t POLL_BACKOFF_MAX = 300000; // error backoff ceiling
const uint32_t METRICS_EVERY_MS = 30UL * 60UL * 1000UL;
const uint32_t METRICS_RETRY_MS = 5UL * 60UL * 1000UL;
const uint32_t STALE_AFTER_MS = 150000; // no fresh data -> stale banner

BabyStatus curStatus = BS_NONE;
time_t sinceEpoch = 0; // when the current status began (UTC)
bool cribBounce = false;
bool cribMusic = false;
uint32_t lastOkMs = 0; // millis() of last good status poll
uint32_t nextStatusMs = 0;
uint32_t nextMetricsMs = 0;
uint32_t statusBackoff = 0; // current error backoff (0 = none)
bool tokenExpired = false;  // 401
bool subInactive = false;   // 403
bool firstStatus = true;    // no chime for the boot-time status
int caIdx = 0;              // which root CA we present (0=Amazon, 1=ISRG)
bool caOk = false;          // a request has succeeded with caIdx
uint32_t nextDrawMs = 0;    // next screen redraw; 0 forces one now

DayMetrics metrics;

// ---------------- night score (lamp page) ----------------
// The lamp is for the off-shift parent: one constant color for how the night
// is going. Sleeping AND stirring count as good (stirring usually resolves
// itself); awake, crying, and away count against.
Page page = PAGE_STATUS; // persisted; lamp survives reboots
bool nightActive = false;
uint32_t nsGoodSecs = 0, nsBadSecs = 0; // counted seconds, good vs bad
int nsWakings = 0;                      // good -> awake/crying/away transitions
time_t nightStartEpoch = 0;
bool nsBackfillTried = false; // one c-chart catch-up after a reboot
uint32_t lampPeekUntil = 0;   // tap shows numbers briefly

// ---------------- chimes: short square-wave ocarina motifs ----------------
// OoT ocarina buttons map to D4 F4 A4 B4 D5; each song is its 6-7 note motif.
// Each note carries its own rest: short between notes in a phrase, a real
// breath after a phrase's held note, and the final note rings out.
const Note N_LULLABY[] = {{494, 330, 40}, {587, 330, 40}, {440, 650, 220},
                          {494, 330, 40}, {587, 330, 40}, {440, 800, 0}}; // B D A  B D A
const Note N_NOCTURNE[] = {{494, 210, 30}, {440, 210, 30}, {440, 210, 30}, {294, 210, 30},
                           {494, 210, 30}, {440, 210, 30}, {349, 500, 0}}; // B A A D B A F
const Note N_SUNS[] = {{440, 150, 25}, {349, 150, 25}, {587, 330, 170},
                       {440, 150, 25}, {349, 150, 25}, {587, 450, 0}}; // A F D'  A F D'
const Note N_STORMS[] = {{294, 150, 25}, {349, 150, 25}, {587, 430, 170},
                         {294, 150, 25}, {349, 150, 25}, {587, 560, 0}}; // D F D'  D F D'
const Note N_SARIA[] = {{349, 150, 25}, {440, 150, 25}, {494, 330, 170},
                        {349, 150, 25}, {440, 150, 25}, {494, 450, 0}}; // F A B  F A B
const Song SONGS[SONG_COUNT] = {
    {N_LULLABY, 6}, {N_NOCTURNE, 7}, {N_SUNS, 6}, {N_STORMS, 6}, {N_SARIA, 6},
};
// volume: index 0 = mute, 1-3 = low/medium/high (waves on the speaker icon)
const uint8_t VOL_LEVELS[4] = {0, 70, 140, 210};
int volIdx = 2; // default: medium

bool inNightWindow(); // defined below; used by the chime player

bool isMuted() { return volIdx == 0; }
uint8_t currentVolume() { // night window ducks whatever level is set
  uint8_t v = VOL_LEVELS[volIdx];
  return inNightWindow() ? (uint8_t)(v * 2 / 5) : v;
}

int playingSong = -1; // index into SONGS, -1 = idle
uint8_t noteIdx = 0;
uint32_t noteNextMs = 0;
bool cryAcked = false; // tap during crying silences the loop
uint32_t cryNextLoopMs = 0;

bool nightDimActive = false; // set each frame; ducks chime volume

void stopSong() {
  playingSong = -1;
  M5.Speaker.stop();
}

void playSong(int id) {
  if (isMuted() || id < 0 || id >= SONG_COUNT)
    return;
  playingSong = id;
  noteIdx = 0;
  noteNextMs = millis();
}

void serviceSong() {
  if (playingSong < 0)
    return;
  if ((int32_t)(millis() - noteNextMs) < 0)
    return;
  const Song &s = SONGS[playingSong];
  if (noteIdx >= s.len) {
    playingSong = -1;
    return;
  }
  const Note &n = s.notes[noteIdx];
  M5.Speaker.setVolume(currentVolume());
  M5.Speaker.tone(n.freq, n.ms);
  noteNextMs = millis() + n.ms + n.gapMs;
  noteIdx++;
}

int songForStatus(BabyStatus st) {
  switch (st) {
  case BS_SLEEPING:
    return SONG_LULLABY;
  case BS_STIRRING:
    return SONG_NOCTURNE;
  case BS_AWAKE:
    return SONG_SUNS;
  case BS_CRYING:
    return SONG_STORMS;
  case BS_AWAY:
    return SONG_SARIA;
  default:
    return -1;
  }
}

// ---------------- non-blocking haptics (same pattern as Breathe) ----------------
uint32_t vibeOff = 0;
void buzz(uint32_t ms, uint8_t power = 120) {
  M5.Power.setVibration(power);
  vibeOff = millis() + ms;
}
void serviceVibe() {
  if (vibeOff && (int32_t)(millis() - vibeOff) >= 0) {
    M5.Power.setVibration(0);
    vibeOff = 0;
  }
}

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097L + (long)doe - 719468L;
}

// "2026-03-12T22:15:00Z" (optional fractional seconds) -> epoch, 0 on failure
time_t parseIso8601Utc(const String &s) {
  int y, mo, d, h, mi, sec;
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6 || y < 1970 ||
      mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60)
    return 0;
  int pos = 19;
  if (s[pos] == '.') {
    pos++;
    while (pos < (int)s.length() && s[pos] >= '0' && s[pos] <= '9')
      pos++;
  }
  long offset = 0;
  if (s[pos] == '+' || s[pos] == '-') {
    int oh, om;
    if (sscanf(s.c_str() + pos + 1, "%d:%d", &oh, &om) != 2 || oh > 23 || om > 59)
      return 0;
    offset = (oh * 60 + om) * 60 * (s[pos] == '+' ? 1 : -1);
  } else if (s[pos] != 'Z')
    return 0;
  return (time_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - offset;
}

bool clockSynced() { return ntpSynced; }
// ---------------- derived state ----------------
DisplayState displayState() {
  if (sourceAge() > 900 || sourceError.length())
    return DS_BOOT;
  switch (curStatus) {
  case BS_SLEEPING: {
    long asleep = (sinceEpoch && clockSynced()) ? (long)(time(nullptr) - sinceEpoch) : -1;
    return (asleep >= 0 && asleep < (long)SETTLED_SECS) ? DS_SETTLING : DS_ASLEEP;
  }
  case BS_AWAKE:
    return DS_AWAKE;
  case BS_STIRRING:
    return DS_STIRRING;
  case BS_CRYING:
    return DS_CRYING;
  case BS_AWAY:
    return DS_AWAY;
  default:
    return DS_BOOT;
  }
}

// bedtime .. rise time (falls back to 20:30 .. 07:00 until metrics arrive)
bool inNightWindow() {
  if (!clockSynced())
    return false;
  int bed = metrics.bedMin >= 0 ? metrics.bedMin : 20 * 60 + 30;
  int rise = metrics.riseMin >= 0 ? metrics.riseMin : 7 * 60;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  int m = lt.tm_hour * 60 + lt.tm_min;
  return bed > rise ? (m >= bed || m < rise) : (m >= bed && m < rise);
}

// epoch of the most recent bedtime boundary (tonight's, or yesterday's if
// we're past midnight)
time_t nightWindowStart() {
  time_t now = time(nullptr);
  struct tm st;
  localtime_r(&now, &st);
  int bed = metrics.bedMin >= 0 ? metrics.bedMin : 20 * 60 + 30;
  st.tm_hour = bed / 60;
  st.tm_min = bed % 60;
  st.tm_sec = 0;
  st.tm_isdst = -1;
  time_t s = mktime(&st);
  if (s > now)
    s -= 86400;
  return s;
}

String fmtDur(long secs) {
  if (secs < 0)
    return "";
  if (secs < 60)
    return String(secs) + "s";
  if (secs < 3600)
    return String(secs / 60) + "m";
  return String(secs / 3600) + "h " + String((secs % 3600) / 60) + "m";
}

String clockStr(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  int h12 = lt.tm_hour % 12;
  if (h12 == 0)
    h12 = 12;
  char b[16];
  snprintf(b, sizeof(b), "%d:%02d %s", h12, lt.tm_min, lt.tm_hour < 12 ? "am" : "pm");
  return String(b);
}

// ---------------- UI ----------------
Preferences prefs;
M5Canvas canvas(&M5.Display);

const Box BOX_MUTE{0, 0, 42, 36};    // top-right corner, generous reach
const Box BOX_WORD{40, 78, 240, 84}; // the state name -> replay chime

// calm = settled-green with no recent touch, outside the night window
const uint8_t BRIGHT_DAY = 90, BRIGHT_CALM = 45, BRIGHT_ALERT = 255, BRIGHT_NIGHT = 12;
const uint8_t BRIGHT_LAMP = 50; // lamp page: constant, never overridden
uint32_t nightWakeUntil = 0;    // any touch holds full display this long

// level 0 = muted (X); 1-3 = that many waves
void drawSpeakerIcon(int x, int y, uint16_t col, int level) {
  canvas.fillRect(x - 9, y - 4, 5, 9, col);                       // body
  canvas.fillTriangle(x - 5, y, x + 2, y - 8, x + 2, y + 8, col); // cone
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

void drawMoon(int cx, int cy, uint16_t fg, uint16_t bg) {
  canvas.fillCircle(cx, cy, 13, fg);
  canvas.fillCircle(cx + 6, cy - 5, 11, bg);
}
void drawWaves(int cx, int cy, uint16_t fg) {
  for (int row = -5; row <= 5; row += 10)
    for (int o = 0; o < 2; ++o) {
      canvas.fillArc(cx - 7, cy + row + o, 7, 6, 180, 360, fg);
      canvas.fillArc(cx + 7, cy + row + o, 7, 6, 0, 180, fg);
    }
}
void drawSun(int cx, int cy, uint16_t fg) {
  canvas.fillCircle(cx, cy, 7, fg);
  for (int i = 0; i < 8; ++i) {
    float a = i * PI / 4.0f;
    canvas.drawLine(cx + (int)(cosf(a) * 10), cy + (int)(sinf(a) * 10), cx + (int)(cosf(a) * 14),
                    cy + (int)(sinf(a) * 14), fg);
  }
}
void drawBell(int cx, int cy, uint16_t fg) {
  canvas.fillArc(cx, cy + 3, 11, 0, 180, 360, fg);
  canvas.fillRect(cx - 12, cy + 3, 24, 3, fg);
  canvas.fillCircle(cx, cy + 9, 3, fg);
  canvas.fillCircle(cx, cy - 9, 2, fg);
}
void drawCrib(int cx, int cy, uint16_t fg) {
  for (int dx = -12; dx <= 12; dx += 6)
    canvas.drawFastVLine(cx + dx, cy - 10, 21, fg);
  canvas.drawFastHLine(cx - 14, cy - 6, 29, fg);
  canvas.drawFastHLine(cx - 14, cy + 7, 29, fg);
}
void drawBounceGlyph(int x, int y, uint16_t col) {
  for (int o = 0; o < 2; ++o) {
    canvas.drawLine(x - 5, y - 2 + o, x, y - 7 + o, col);
    canvas.drawLine(x, y - 7 + o, x + 5, y - 2 + o, col);
    canvas.drawLine(x - 5, y + 2 + o, x, y + 7 + o, col);
    canvas.drawLine(x, y + 7 + o, x + 5, y + 2 + o, col);
  }
}
void drawNoteGlyph(int x, int y, uint16_t col) {
  canvas.fillCircle(x - 2, y + 5, 3, col);
  canvas.drawFastVLine(x + 1, y - 6, 11, col);
  canvas.drawFastVLine(x + 2, y - 6, 5, col);
}

void drawPageDots(uint16_t on, uint16_t off) {
  const int xs[3] = {280, 294, 308};
  for (int i = 0; i < 3; ++i)
    canvas.fillCircle(xs[i], 222, 3, (int)page == i ? on : off);
}

// A one-pixel highlight and shadow makes panels read like 16-bit-era menu tiles.
void pixelPanel(int x, int y, int w, int h, uint16_t fill, uint16_t edge, uint16_t shadow) {
  canvas.fillRect(x + 2, y + 2, w - 2, h - 2, shadow);
  canvas.fillRect(x, y, w - 2, h - 2, fill);
  canvas.drawFastHLine(x, y, w - 2, edge);
  canvas.drawFastVLine(x, y, h - 2, edge);
}

// warning banner across the top; returns true if one was drawn
bool drawBanner() {
  String msg;
  if (sourceError.length())
    msg = "Crib: " + sourceError;
  else if (!sourceObserved)
    msg = "Crib data unavailable";
  else if (sourceAge() > 180)
    msg = "Crib data " + String(sourceAge() / 60) + " min old";
  else if (WiFi.status() != WL_CONNECTED)
    msg = "Wi-Fi lost - reconnecting";
  else
    return false;
  canvas.fillRect(0, 0, 320, 30, COL_BANNER);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_AMBER);
  canvas.drawString(msg, 160, 15);
  return true;
}

void drawStatusScreen() {
  DisplayState ds = displayState();

  uint16_t bg, fg; // fg = full-strength text/icon color on bg
  const char *word;
  switch (ds) {
  case DS_ASLEEP:
    bg = COL_GREEN;
    fg = COL_WHITE;
    word = "Asleep";
    break;
  case DS_SETTLING:
    bg = COL_YELLOW;
    fg = COL_YELTX;
    word = "Settling";
    break;
  case DS_STIRRING:
    bg = COL_YELLOW;
    fg = COL_YELTX;
    word = "Stirring";
    break;
  case DS_AWAKE:
    bg = COL_ORANGE;
    fg = COL_WHITE;
    word = "Awake";
    break;
  case DS_CRYING:
    bg = COL_RED;
    fg = COL_WHITE;
    word = "Crying";
    break;
  case DS_AWAY:
    bg = COL_AWAYBG;
    fg = COL_AWAYTX;
    word = "Away";
    break;
  default:
    bg = COL_DARK;
    fg = COL_WHITE;
    word = "Unknown";
    break;
  }
  uint16_t dim = lerpCol(bg, fg, 0.75f); // ~75% strength, like the mockups
  uint16_t faint = lerpCol(bg, fg, 0.55f);

  canvas.fillScreen(bg);

  // crying: expanding ring behind everything
  if (ds == DS_CRYING) {
    int r = 40 + (int)((millis() % 1100) * 110 / 1100);
    uint16_t ring = lerpCol(bg, COL_WHITE, 0.8f);
    for (int o = 0; o < 3; ++o)
      canvas.drawCircle(160, 120, r + o, ring);
  }

  // top: banner if something's wrong, else clock + mute toggle
  if (!drawBanner()) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(dim);
    if (clockSynced())
      canvas.drawString(clockStr(time(nullptr)), 65, 17);
  }
  drawSpeakerIcon(20, 18, lerpCol(bg, fg, 0.75f), volIdx);

  // center: icon + state word + duration
  switch (ds) {
  case DS_ASLEEP:
  case DS_SETTLING:
    drawMoon(160, 72, dim, bg);
    break;
  case DS_STIRRING:
    drawWaves(160, 72, dim);
    break;
  case DS_AWAKE:
    drawSun(160, 72, dim);
    break;
  case DS_CRYING:
    drawBell(160, 72, fg);
    break;
  case DS_AWAY:
    drawCrib(160, 72, dim);
    break;
  default:
    break;
  }
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextColor(fg);
  canvas.drawString(word, 160, 118);

  String sub;
  if (sinceEpoch && clockSynced()) {
    long secs = (long)(time(nullptr) - sinceEpoch);
    if (ds == DS_AWAY)
      sub = "since " + clockStr(sinceEpoch);
    else if (ds == DS_SETTLING)
      sub = "asleep for " + fmtDur(secs);
    else
      sub = "for " + fmtDur(secs);
  }
  canvas.setFont(&fonts::FreeSans12pt7b);
  canvas.setTextColor(dim);
  canvas.drawString(sub, 160, 158);

  // crying: acknowledge pill
  if (ds == DS_CRYING) {
    const char *pill = cryAcked ? "chime silenced" : "tap to silence chime";
    canvas.setFont(&fonts::FreeSans9pt7b);
    int pw = canvas.textWidth(pill) + 28;
    canvas.fillRoundRect(160 - pw / 2, 196, pw, 26, 13, lerpCol(bg, COL_WHITE, 0.92f));
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(rgb565(124, 18, 18));
    canvas.drawString(pill, 160, 209);
  } else {
    // footer: crib activity, else tonight's bedtime; page dots right
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(dim);
    int x = 14;
    if (cribBounce) {
      drawBounceGlyph(x + 5, 221, dim);
      canvas.drawString("bouncing", x + 16, 222);
      x += 16 + canvas.textWidth("bouncing") + 16;
    }
    if (cribMusic) {
      drawNoteGlyph(x + 4, 221, dim);
      canvas.drawString("sound", x + 14, 222);
      x += 14 + canvas.textWidth("sound") + 16;
    }
    if (x == 14 && ds == DS_ASLEEP && metrics.bed.length())
      canvas.drawString("Bed " + metrics.bed, 14, 222);
    drawPageDots(fg, faint);
  }

  drawBanner();
  canvas.pushSprite(0, 0);
}

void drawStatsScreen() {
  canvas.fillScreen(COL_DARK);

  if (!drawBanner()) {
    // battery, top-left: outline glyph with fill level + percentage
    int batt = M5.Power.getBatteryLevel();
    if (batt < 0)
      batt = 0;
    if (batt > 100)
      batt = 100;
    bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
    uint16_t bcol = (batt <= 20 && !charging) ? COL_AMBER : COL_DIM;
    canvas.drawRoundRect(14, 10, 24, 14, 3, bcol);
    canvas.fillRect(38, 14, 3, 6, bcol);               // terminal nub
    canvas.fillRect(17, 13, batt * 18 / 100, 8, bcol); // fill level
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(bcol);
    canvas.drawString(String(batt) + "%" + (charging ? " +" : ""), 46, 17);
    canvas.setTextColor(COL_DIM);
    canvas.setTextDatum(middle_right);
    if (clockSynced()) {
      time_t now = time(nullptr);
      struct tm lt;
      localtime_r(&now, &lt);
      char d[24];
      strftime(d, sizeof(d), "%a %b %d", &lt);
      canvas.drawString(d, 250, 17);
    }
  }
  drawSpeakerIcon(20, 18, COL_DIM, volIdx);

  const char *labels[6] = {"RISE TIME", "BEDTIME", "LONGEST", "NAPS", "SOOTHES", "AWAKE IN BED"};
  String vals[6] = {metrics.rise, metrics.bed,     metrics.longest,
                    metrics.naps, metrics.soothes, metrics.awakeInBed};
  for (int i = 0; i < 6; ++i) {
    int col = i % 2, row = i / 2;
    int x = 12 + col * 156, y = 34 + row * 56;
    canvas.fillRoundRect(x, y, 148, 50, 10, COL_PANEL);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(labels[i], x + 12, y + 9);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextColor(COL_WHITE);
    canvas.drawString(vals[i].length() ? vals[i] : "--", x + 12, y + 22);
  }

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(metrics.inBed.length() ? "In bed " + metrics.inBed : "", 14, 222);
  drawPageDots(COL_WHITE, COL_FAINT);

  drawBanner();
  canvas.pushSprite(0, 0);
}

void drawNightScreen() {
  canvas.fillScreen(COL_BLACK);
  const float glow[4] = {0.05f, 0.09f, 0.14f, 0.22f};
  const int rad[4] = {78, 58, 40, 24};
  for (int i = 0; i < 4; ++i)
    canvas.fillCircle(160, 104, rad[i], lerpCol(COL_BLACK, COL_GREEN, glow[i]));
  canvas.fillCircle(160, 104, 6, COL_NIGHTDOT);
  String sub = "Asleep";
  if (sinceEpoch && clockSynced())
    sub += "  " + fmtDur((long)(time(nullptr) - sinceEpoch));
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_NIGHT);
  canvas.drawString(sub, 160, 168);
  drawBanner();
  canvas.pushSprite(0, 0);
}

// score -> color: >=85% is a great baby night (deep green); the band slides
// through amber down to a soft ember at 50%, and stays ember below — no
// alarm-red about a hard night someone is already living
uint16_t scoreColor(float p) {
  const uint16_t GOODC = rgb565(18, 160, 88);
  const uint16_t MIDC = rgb565(227, 168, 11);
  const uint16_t LOWC = rgb565(200, 72, 28);
  if (p >= 0.85f)
    return GOODC;
  if (p >= 0.675f)
    return lerpCol(MIDC, GOODC, (p - 0.675f) / 0.175f);
  if (p >= 0.50f)
    return lerpCol(LOWC, MIDC, (p - 0.50f) / 0.175f);
  return LOWC;
}

void drawLampScreen() {
  uint32_t tot = nsGoodSecs + nsBadSecs;
  bool haveScore = nightActive && tot >= 45 * 60 && sourceAge() <= 900 &&
                   !sourceError.length(); // small denominators swing wildly
  uint16_t col = haveScore ? scoreColor((float)nsGoodSecs / (float)tot)
                           : rgb565(64, 74, 100); // neutral: night not underway yet

  canvas.fillScreen(COL_BLACK);
  const float glow[5] = {0.10f, 0.18f, 0.30f, 0.48f, 0.80f};
  const int rad[5] = {150, 120, 95, 72, 50};
  for (int i = 0; i < 5; ++i)
    canvas.fillCircle(160, 120, rad[i], lerpCol(COL_BLACK, col, glow[i]));

  if ((int32_t)(millis() - lampPeekUntil) < 0) { // tap-to-peek overlay
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_WHITE);
    if (haveScore) {
      int pct = (int)(100.0f * nsGoodSecs / tot + 0.5f);
      canvas.setFont(&fonts::FreeSansBold24pt7b);
      canvas.drawString(String(pct) + "%", 160, 96);
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.drawString(
          "settled " + fmtDur((long)nsGoodSecs) + "   awake " + fmtDur((long)nsBadSecs), 160, 148);
      canvas.drawString(String(nsWakings) + (nsWakings == 1 ? " waking" : " wakings"), 160, 172);
    } else {
      canvas.setFont(&fonts::FreeSans12pt7b);
      canvas.drawString(nightActive ? String("night just started")
                                    : "night starts " +
                                          (metrics.bed.length() ? metrics.bed : String("8:30 pm")),
                        160, 120);
    }
  }
  drawBanner();
  canvas.pushSprite(0, 0);
}

void drawBootScreen(const char *msg) {
  canvas.fillScreen(COL_DARK);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(COL_WHITE);
  canvas.drawString("NurseryPad", 160, 96);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(msg, 160, 140);
  drawBanner();
  canvas.pushSprite(0, 0);
}
