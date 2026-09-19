// Adapted from CradleWatch: chimes, haptics, derived crib state, and the dashboard-mode
// renderers (status, dimmed night, lamp). Restyled to the design canvas. No network calls.
#pragma once

const uint32_t SETTLED_SECS = 10 * 60; // sleeping this long -> settled green

BabyStatus curStatus = BS_NONE;
time_t sinceEpoch = 0; // when the current status began (UTC)
bool cribBounce = false;
bool cribMusic = false;
bool firstStatus = true; // no chime for the boot-time status

DayMetrics metrics;

// ---------------- night score (lamp page) ----------------
// The lamp is for the off-shift parent: one constant color for how the night is going.
// Sleeping AND stirring count as good (stirring usually resolves itself); awake, crying,
// and away count against.
Page page = PAGE_STATUS; // persisted; lamp survives reboots
bool nightActive = false;
uint32_t nsGoodSecs = 0, nsBadSecs = 0; // counted seconds, good vs bad
int nsWakings = 0;                      // good -> awake/crying/away transitions
uint32_t lampPeekUntil = 0;             // tap shows numbers briefly
String dashFooter;                      // "fed 2h 10m ago · wet 45m ago · naps today 2"

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

// ---------------- non-blocking haptics ----------------
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

// "2026-03-12T22:15:00Z" (optional fractional seconds, optional offset) -> epoch, 0 on failure
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
    long asleep = (sinceEpoch && nowEpoch()) ? (long)(nowEpoch() - sinceEpoch) : -1;
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
/** Seconds the crib has been in its current state, or -1 without a usable clock. */
long sinceSecs() { return (sinceEpoch && nowEpoch()) ? max(0L, (long)(nowEpoch() - sinceEpoch)) : -1; }

const char *stateWord(DisplayState ds) {
  switch (ds) {
  case DS_ASLEEP:
    return "Asleep";
  case DS_SETTLING:
    return "Settling";
  case DS_STIRRING:
    return "Stirring";
  case DS_AWAKE:
    return "Awake";
  case DS_CRYING:
    return "Crying";
  case DS_AWAY:
    return "Not in crib";
  default:
    return "No crib data";
  }
}
/** Background for a state; `ink` receives the readable text colour on it. */
uint16_t stateColor(DisplayState ds, uint16_t &ink) {
  ink = C_WHITE;
  switch (ds) {
  case DS_ASLEEP:
    return C_ASLEEP;
  case DS_SETTLING:
  case DS_STIRRING:
    ink = C_YELLOW_INK;
    return C_SETTLING;
  case DS_AWAKE:
    return C_AWAKE;
  case DS_CRYING:
    return C_CRYING;
  case DS_AWAY:
    return C_AWAY;
  default:
    ink = C_TEXT;
    return C_BG;
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

// ---------------- UI ----------------
Preferences prefs;

const uint8_t BRIGHT_DAY = 90, BRIGHT_ALERT = 255, BRIGHT_NIGHT = 12;
const uint8_t BRIGHT_LAMP = 50; // lamp page: constant, never overridden
uint32_t nightWakeUntil = 0;    // any touch holds full display this long

/** Something is wrong with the data feed; returns the line to show, empty when all is well. */
String bannerText() {
  if (sourceError.length())
    return "Crib: " + sourceError;
  if (!sourceObserved)
    return "Crib data unavailable";
  if (sourceAge() > 180)
    return "Crib data " + String(sourceAge() / 60) + " min old";
  if (WiFi.status() != WL_CONNECTED)
    return "Wi-Fi lost - reconnecting";
  return "";
}

/** Dashboard mode: the whole screen is the crib state. Tap for Home, hold for the lamp. */
void drawStatusScreen() {
  DisplayState ds = displayState();
  uint16_t ink;
  uint16_t bg = stateColor(ds, ink);
  uint16_t dim = lerpCol(bg, ink, 0.80f);
  canvas.fillScreen(bg);
  hit(0, 0, SCR_W, SCR_H, A_DASH_TAP); // registered first so the corner buttons win

  // crying: expanding ring behind everything
  if (ds == DS_CRYING) {
    int r = 44 + (int)((millis() % 1100) * 100 / 1100);
    uint16_t ring = lerpCol(bg, C_WHITE, 0.85f);
    for (int o = 0; o < 4; ++o)
      canvas.drawCircle(160, 116, r + o, ring);
  }

  // top row: speaker, hint or banner, home circle
  iconSpeaker(22, 18, dim, volIdx);
  hit(0, 0, 44, BAR_H, A_VOL);
  String banner = bannerText();
  text(fit(banner.length() ? banner : "tap for Home  ·  hold for lamp", F_SMALL, 200), 160, 18, F_SMALL,
       banner.length() ? C_WHITE : dim, bg);
  canvas.fillCircle(298, 18, 13, dim);
  canvas.fillCircle(298, 18, 11, bg);
  iconHome(298, 18, dim);
  hit(276, 0, 44, BAR_H, A_DASH_HOME);

  // centre: state word and how long
  text(stateWord(ds), 160, 106, F_HERO, ink, bg);
  long secs = sinceSecs();
  String sub;
  if (secs >= 0 && ds != DS_BOOT)
    sub = "since " + clockStr(sinceEpoch) + "  ·  " + fmtDur(secs);
  text(sub, 160, 146, F_SMALL, dim, bg);

  if (ds == DS_CRYING) {
    const char *pill = cryAcked ? "chime silenced" : "tap to silence chime";
    int pw = textW(pill, F_SMALL) + 28;
    uint16_t pillBg = lerpCol(bg, C_WHITE, 0.92f);
    canvas.fillRoundRect(160 - pw / 2, 190, pw, 26, 13, pillBg);
    text(pill, 160, 203, F_SMALL, rgb565(124, 18, 18), pillBg);
  } else {
    // footer: crib activity, then the day's log line
    String foot;
    if (cribBounce)
      foot += "bouncing";
    if (cribMusic)
      foot += String(foot.length() ? "  ·  " : "") + "sound";
    if (dashFooter.length())
      foot += String(foot.length() ? "  ·  " : "") + dashFooter;
    text(fit(foot, F_SMALL, SCR_W - 2 * PAD), 160, 222, F_SMALL, dim, bg);
  }
}

/** Settled night sleep, lights nearly off: one dot, one line. */
void drawNightScreen() {
  canvas.fillScreen(C_BLACK);
  hit(0, 0, SCR_W, SCR_H, A_DASH_TAP);
  const float glow[4] = {0.05f, 0.09f, 0.14f, 0.22f};
  const int rad[4] = {78, 58, 40, 24};
  for (int i = 0; i < 4; ++i)
    canvas.fillCircle(160, 104, rad[i], lerpCol(C_BLACK, C_ASLEEP, glow[i]));
  canvas.fillCircle(160, 104, 6, C_NIGHTDOT);
  String sub = "Asleep";
  long secs = sinceSecs();
  if (secs >= 0)
    sub += "  " + fmtDur(secs);
  text(sub, 160, 168, F_SMALL, C_NIGHT, C_BLACK);
  String banner = bannerText();
  if (banner.length())
    text(banner, 160, 18, F_SMALL, C_MUTED, C_BLACK);
}

// score -> color: >=85% is a great baby night (deep green); the band slides
// through amber down to a soft ember at 50%, and stays ember below - no
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

/** Lamp: one colour for how the night is going. Tap peeks at the numbers, hold returns. */
void drawLampScreen() {
  uint32_t tot = nsGoodSecs + nsBadSecs;
  bool haveScore = nightActive && tot >= 45 * 60 && sourceAge() <= 900 &&
                   !sourceError.length(); // small denominators swing wildly
  uint16_t col = haveScore ? scoreColor((float)nsGoodSecs / (float)tot)
                           : rgb565(64, 74, 100); // neutral: night not underway yet

  canvas.fillScreen(C_BLACK);
  hit(0, 0, SCR_W, SCR_H, A_DASH_TAP);
  const float glow[5] = {0.10f, 0.18f, 0.30f, 0.48f, 0.80f};
  const int rad[5] = {150, 120, 95, 72, 50};
  for (int i = 0; i < 5; ++i)
    canvas.fillCircle(160, 120, rad[i], lerpCol(C_BLACK, col, glow[i]));

  if ((int32_t)(millis() - lampPeekUntil) < 0) { // tap-to-peek overlay
    if (haveScore) {
      int pct = (int)(100.0f * nsGoodSecs / tot + 0.5f);
      text(String(pct) + "%", 160, 96, F_HERO, C_WHITE, col);
      text("settled " + fmtDur((long)nsGoodSecs) + "   awake " + fmtDur((long)nsBadSecs), 160, 148, F_SMALL,
           C_WHITE, col);
      text(String(nsWakings) + (nsWakings == 1 ? " waking" : " wakings"), 160, 172, F_SMALL, C_WHITE, col);
    } else
      text(nightActive ? String("night just started")
                       : "night starts " + (metrics.bed.length() ? metrics.bed : String("20:30")),
           160, 120, F_BODY, C_WHITE, col);
  }
  String banner = bannerText();
  if (banner.length())
    text(banner, 160, 18, F_SMALL, C_MUTED, C_BLACK);
}
