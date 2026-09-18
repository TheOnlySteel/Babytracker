/*
 *  CradleWatch — a glanceable Cradlewise sleep dashboard for the M5Stack Core2
 *
 *  Polls the Cradlewise Data API (read-only, bearer token) and turns the
 *  whole screen into the baby's status color, readable across a dark room:
 *
 *    green      asleep and settled (> 10 min)
 *    yellow     settling (just fell asleep) or stirring
 *    orange     awake
 *    red        crying — pulsing ring + a looping chime until tapped/muted
 *    grey-blue  not in the crib
 *
 *  Chimes (short square-wave ocarina motifs) mark every state change:
 *    asleep = Zelda's Lullaby        stirring = Nocturne of Shadow
 *    awake  = Sun's Song             crying   = Song of Storms (loops)
 *    away   = Saria's Song
 *
 *  Controls:
 *    top-right corner       mute toggle (saved to flash)
 *    tap the state name     replay the current state's chime
 *    tap during crying      silence the looping chime (until the next cry)
 *    tap elsewhere / BtnA-C flip between status and today's stats
 *
 *  Night mode: while asleep between bedtime and rise time the screen drops
 *  to a faint glow; any tap wakes it for 15 s. Crying overrides everything.
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>
#include "secrets.h"           // copy secrets.example.h -> secrets.h and fill in
#include "cradlewise_certs.h"
#include "cradlewatch_types.h"

// ---------------- palette (RGB565), from the design mockups ----------------
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t COL_GREEN   = rgb565( 15, 138,  78);
const uint16_t COL_YELLOW  = rgb565(227, 168,  11);
const uint16_t COL_ORANGE  = rgb565(226,  96,  27);
const uint16_t COL_RED     = rgb565(212,  43,  43);
const uint16_t COL_AWAYBG  = rgb565( 38,  48,  62);
const uint16_t COL_AWAYTX  = rgb565(175, 194, 216);
const uint16_t COL_DARK    = rgb565( 12,  12,  16);   // stats page background
const uint16_t COL_PANEL   = rgb565( 24,  24,  28);   // stats tiles
const uint16_t COL_DIM     = rgb565(140, 140, 148);
const uint16_t COL_FAINT   = rgb565( 60,  60,  66);
const uint16_t COL_WHITE   = rgb565(245, 245, 245);
const uint16_t COL_YELTX   = rgb565( 33,  25,   0);   // dark text on yellow
const uint16_t COL_AMBER   = rgb565(255, 210, 122);   // stale-banner text
const uint16_t COL_BANNER  = rgb565( 38,  38,  38);
const uint16_t COL_NIGHT   = rgb565( 46, 143,  94);   // night-mode text
const uint16_t COL_NIGHTDOT= rgb565( 53, 199, 127);
const uint16_t COL_BLACK   = rgb565(  0,   0,   0);

uint16_t lerpCol(uint16_t a, uint16_t b, float t) {
  if (t <= 0.0f) return a;
  if (t >= 1.0f) return b;
  int ar = a >> 11, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = b >> 11, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  return (uint16_t)(((int)(ar + (br - ar) * t) << 11) |
                    ((int)(ag + (bg - ag) * t) << 5)  |
                     (int)(ab + (bb - ab) * t));
}

// ---------------- Cradlewise Data API ----------------
const char* CW_BASE = "https://integrations.cradlewise.com/api/v1";

const uint32_t SETTLED_SECS      = 10 * 60;   // sleeping this long -> green
const uint32_t POLL_ACTIVE_MS    = 30000;     // status poll while things happen (API floor)
const uint32_t POLL_CALM_MS      = 60000;     // status poll during settled sleep / away
const uint32_t POLL_BACKOFF_MAX  = 300000;    // error backoff ceiling
const uint32_t METRICS_EVERY_MS  = 30UL * 60UL * 1000UL;
const uint32_t METRICS_RETRY_MS  = 5UL * 60UL * 1000UL;
const uint32_t STALE_AFTER_MS    = 150000;    // no fresh data -> stale banner

BabyStatus curStatus   = BS_NONE;
time_t     sinceEpoch  = 0;                   // when the current status began (UTC)
bool       cribBounce  = false;
bool       cribMusic   = false;
uint32_t   lastOkMs    = 0;                   // millis() of last good status poll
uint32_t   nextStatusMs  = 0;
uint32_t   nextMetricsMs = 0;
uint32_t   statusBackoff = 0;                 // current error backoff (0 = none)
bool       tokenExpired  = false;             // 401
bool       subInactive   = false;             // 403
bool       firstStatus   = true;              // no chime for the boot-time status
int        caIdx = 0;                         // which root CA we present (0=Amazon, 1=ISRG)
bool       caOk  = false;                     // a request has succeeded with caIdx
uint32_t   nextDrawMs = 0;                    // next screen redraw; 0 forces one now

DayMetrics metrics;

// ---------------- night score (lamp page) ----------------
// The lamp is for the off-shift parent: one constant color for how the night
// is going. Sleeping AND stirring count as good (stirring usually resolves
// itself); awake, crying, and away count against.
Page     page             = PAGE_STATUS;      // persisted; lamp survives reboots
bool     nightActive      = false;
uint32_t nsGoodSecs       = 0, nsBadSecs = 0; // counted seconds, good vs bad
int      nsWakings        = 0;                // good -> awake/crying/away transitions
time_t   nightStartEpoch  = 0;
bool     nsBackfillTried  = false;            // one c-chart catch-up after a reboot
uint32_t lampPeekUntil    = 0;                // tap shows numbers briefly

// ---------------- chimes: short square-wave ocarina motifs ----------------
// OoT ocarina buttons map to D4 F4 A4 B4 D5; each song is its 6-7 note motif.
// Each note carries its own rest: short between notes in a phrase, a real
// breath after a phrase's held note, and the final note rings out.
const Note N_LULLABY[]  = { {494,330,40},{587,330,40},{440,650,220},
                            {494,330,40},{587,330,40},{440,800,0} };            // B D A  B D A
const Note N_NOCTURNE[] = { {494,210,30},{440,210,30},{440,210,30},{294,210,30},
                            {494,210,30},{440,210,30},{349,500,0} };            // B A A D B A F
const Note N_SUNS[]     = { {440,150,25},{349,150,25},{587,330,170},
                            {440,150,25},{349,150,25},{587,450,0} };            // A F D'  A F D'
const Note N_STORMS[]   = { {294,150,25},{349,150,25},{587,430,170},
                            {294,150,25},{349,150,25},{587,560,0} };            // D F D'  D F D'
const Note N_SARIA[]    = { {349,150,25},{440,150,25},{494,330,170},
                            {349,150,25},{440,150,25},{494,450,0} };            // F A B  F A B
const Song SONGS[SONG_COUNT] = {
  { N_LULLABY,  6 },
  { N_NOCTURNE, 7 },
  { N_SUNS,     6 },
  { N_STORMS,   6 },
  { N_SARIA,    6 },
};
// volume: index 0 = mute, 1-3 = low/medium/high (waves on the speaker icon)
const uint8_t VOL_LEVELS[4] = { 0, 70, 140, 210 };
int volIdx = 2;                               // default: medium

bool inNightWindow();                         // defined below; used by the chime player

bool isMuted() { return volIdx == 0; }
uint8_t currentVolume() {                     // night window ducks whatever level is set
  uint8_t v = VOL_LEVELS[volIdx];
  return inNightWindow() ? (uint8_t)(v * 2 / 5) : v;
}

int      playingSong  = -1;                   // index into SONGS, -1 = idle
uint8_t  noteIdx      = 0;
uint32_t noteNextMs   = 0;
bool     cryAcked     = false;                // tap during crying silences the loop
uint32_t cryNextLoopMs = 0;

bool nightDimActive = false;                  // set each frame; ducks chime volume

void stopSong() {
  playingSong = -1;
  M5.Speaker.stop();
}

void playSong(int id) {
  if (isMuted() || id < 0 || id >= SONG_COUNT) return;
  playingSong = id;
  noteIdx = 0;
  noteNextMs = millis();
}

void serviceSong() {
  if (playingSong < 0) return;
  if ((int32_t)(millis() - noteNextMs) < 0) return;
  const Song& s = SONGS[playingSong];
  if (noteIdx >= s.len) { playingSong = -1; return; }
  const Note& n = s.notes[noteIdx];
  M5.Speaker.setVolume(currentVolume());
  M5.Speaker.tone(n.freq, n.ms);
  noteNextMs = millis() + n.ms + n.gapMs;
  noteIdx++;
}

int songForStatus(BabyStatus st) {
  switch (st) {
    case BS_SLEEPING: return SONG_LULLABY;
    case BS_STIRRING: return SONG_NOCTURNE;
    case BS_AWAKE:    return SONG_SUNS;
    case BS_CRYING:   return SONG_STORMS;
    case BS_AWAY:     return SONG_SARIA;
    default:          return -1;
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

// ---------------- tiny JSON pickers ----------------
// The responses are small and flat where we look; these tolerate any
// whitespace and ignore fields we don't know (per the API's beta guidance).

// value of  "key": "value"  — search starts at `from`, returns "" if absent
String jsonQuoted(const String& s, const char* key, int from = 0) {
  int k = s.indexOf(key, from);
  if (k < 0) return "";
  int colon = s.indexOf(':', k + strlen(key));
  if (colon < 0) return "";
  int q1 = s.indexOf('"', colon);
  if (q1 < 0) return "";
  int q2 = s.indexOf('"', q1 + 1);
  if (q2 < 0) return "";
  return s.substring(q1 + 1, q2);
}

bool jsonBool(const String& s, const char* key, bool dflt) {
  int k = s.indexOf(key);
  if (k < 0) return dflt;
  int colon = s.indexOf(':', k + strlen(key));
  if (colon < 0) return dflt;
  for (unsigned i = colon + 1; i < s.length(); ++i) {
    char c = s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    return c == 't';
  }
  return dflt;
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
time_t parseIso8601Utc(const String& s) {
  int y, mo, d, h, mi, sec;
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
  return (time_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
}

// "8:30 pm" -> minutes since midnight, -1 on failure
int parseClockMin(const String& s) {
  int h, m;
  if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return -1;
  bool pm = s.indexOf('p') >= 0 || s.indexOf('P') >= 0;
  if (h == 12) h = 0;
  return (h + (pm ? 12 : 0)) * 60 + m;
}

bool clockSynced() { return time(nullptr) > 1600000000; }

// ---------------- HTTPS ----------------
// One GET against the Data API. Returns the HTTP code (negative = transport
// error). On 429, retryAfterSec carries the server's Retry-After.
int cwGET(const String& path, String& body, long& retryAfterSec) {
  retryAfterSec = 0;
  if (WiFi.status() != WL_CONNECTED) return -1000;

  WiFiClientSecure client;
#ifdef CW_TLS_INSECURE
  client.setInsecure();
#else
  client.setCACert(caIdx == 0 ? AMAZON_ROOT_CA1 : ISRG_ROOT_X1);
#endif

  HTTPClient https;
  https.setConnectTimeout(8000);
  https.setTimeout(12000);
  if (!https.begin(client, String(CW_BASE) + path)) return -1001;
  https.addHeader("Authorization", String("Bearer ") + CW_TOKEN);
  const char* keys[] = { "Retry-After" };
  https.collectHeaders(keys, 1);

  int code = https.GET();
  if (code == 200) {
    body = https.getString();
    caOk = true;
  } else if (code == 429) {
    retryAfterSec = https.header("Retry-After").toInt();
  } else if (code < 0 && !caOk) {
    caIdx ^= 1;   // server may chain to the other public root; try it next time
  }
  https.end();
  return code;
}

void pollStatus() {
  String body;
  long retryAfter = 0;
  int code = cwGET("/baby/status", body, retryAfter);

  if (code == 200) {
    tokenExpired = subInactive = false;
    statusBackoff = 0;
    lastOkMs = millis();

    String st = jsonQuoted(body, "\"status\"");
    BabyStatus ns = BS_NONE;
    if      (st == "sleeping") ns = BS_SLEEPING;
    else if (st == "awake")    ns = BS_AWAKE;
    else if (st == "stirring") ns = BS_STIRRING;
    else if (st == "crying")   ns = BS_CRYING;
    else if (st == "away")     ns = BS_AWAY;

    sinceEpoch = parseIso8601Utc(jsonQuoted(body, "\"since\""));
    cribBounce = jsonQuoted(body, "\"bounce\"") == "on";
    cribMusic  = jsonQuoted(body, "\"music\"")  == "on";

    if (ns != BS_NONE && ns != curStatus) {
      if (nightActive &&
          (curStatus == BS_SLEEPING || curStatus == BS_STIRRING) &&
          (ns == BS_AWAKE || ns == BS_CRYING || ns == BS_AWAY))
        nsWakings++;
      curStatus = ns;
      nextDrawMs = 0;                         // show the new state right away
      if (ns == BS_CRYING) { cryAcked = false; cryNextLoopMs = 0; }
      // the lamp page stays silent: the on-shift parent is handling it
      if (!firstStatus && page != PAGE_LAMP) playSong(songForStatus(ns));
    }
    firstStatus = false;

    bool calm = (curStatus == BS_AWAY) ||
                (curStatus == BS_SLEEPING && sinceEpoch && clockSynced() &&
                 (time(nullptr) - sinceEpoch) > (long)SETTLED_SECS);
    nextStatusMs = millis() + (calm ? POLL_CALM_MS : POLL_ACTIVE_MS);
    return;
  }

  if (code == 401) tokenExpired = true;
  if (code == 403) subInactive  = true;
  if (code == 429 && retryAfter > 0) {
    nextStatusMs = millis() + (uint32_t)retryAfter * 1000UL + 2000;
    return;
  }
  statusBackoff = statusBackoff ? min(statusBackoff * 2, POLL_BACKOFF_MAX) : POLL_ACTIVE_MS;
  nextStatusMs = millis() + statusBackoff;
}

// After a mid-night reboot the local tally is empty, so rebuild it once from
// the sleep-session events (a single c-chart call, well inside the rate tier).
// Labels containing "sleep" count as good; anything else counts against.
void backfillNightScore() {
  if (!clockSynced() || !nightStartEpoch) return;
  time_t now = time(nullptr);
  struct tm t1, t2;
  localtime_r(&nightStartEpoch, &t1);
  localtime_r(&now, &t2);
  char q[190];
  snprintf(q, sizeof(q),
           "/sleep/c-chart?start_time=%04d-%02d-%02d%%20%02d:%02d:00&end_time=%04d-%02d-%02d%%20%02d:%02d:00",
           t1.tm_year + 1900, t1.tm_mon + 1, t1.tm_mday, t1.tm_hour, t1.tm_min,
           t2.tm_year + 1900, t2.tm_mon + 1, t2.tm_mday, t2.tm_hour, t2.tm_min);

  String body;
  long retryAfter = 0;
  if (cwGET(q, body, retryAfter) != 200) return;

  uint32_t good = 0, bad = 0;
  int pos = 0;
  time_t prevT = 0;
  bool prevGood = false, have = false;
  while (true) {
    int k = body.indexOf("\"event_label\"", pos);
    if (k < 0) break;
    String label = jsonQuoted(body, "\"event_label\"", k);
    int kt = body.indexOf("\"event_time\"", k);
    if (kt < 0) break;
    String ts = jsonQuoted(body, "\"event_time\"", kt);
    int Y, Mo, D, H, Mi, S;
    if (sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) == 6) {
      struct tm tv = {};
      tv.tm_year = Y - 1900; tv.tm_mon = Mo - 1; tv.tm_mday = D;
      tv.tm_hour = H; tv.tm_min = Mi; tv.tm_sec = S; tv.tm_isdst = -1;
      time_t et = mktime(&tv);
      if (have && prevT > 0 && et > prevT) {
        if (prevGood) good += (uint32_t)(et - prevT); else bad += (uint32_t)(et - prevT);
      }
      prevT = et;
      prevGood = label.indexOf("sleep") >= 0;
      have = true;
    }
    pos = kt + 12;
  }
  if (have && prevT > 0 && now > prevT) {     // last event runs to now
    if (prevGood) good += (uint32_t)(now - prevT); else bad += (uint32_t)(now - prevT);
  }
  if (good + bad > 0) { nsGoodSecs += good; nsBadSecs += bad; }
}

// Fetch today's seven key metrics. Range spans yesterday+today so the current
// day-start window (8am-8am by default) is always covered; we keep the LAST
// occurrence of each banner, i.e. the most recent day in the range.
void pollDayMetrics() {
  if (!clockSynced()) { nextMetricsMs = millis() + 15000; return; }

  time_t now = time(nullptr);
  time_t start = now - 36L * 3600L;
  struct tm t1, t2;
  localtime_r(&start, &t1);
  localtime_r(&now, &t2);
  char q[160];
  snprintf(q, sizeof(q),
           "/sleep/day-metrics?start_time=%04d-%02d-%02d%%20%02d:00:00&end_time=%04d-%02d-%02d%%20%02d:%02d:00",
           t1.tm_year + 1900, t1.tm_mon + 1, t1.tm_mday, t1.tm_hour,
           t2.tm_year + 1900, t2.tm_mon + 1, t2.tm_mday, t2.tm_hour, t2.tm_min);

  String body;
  long retryAfter = 0;
  int code = cwGET(q, body, retryAfter);
  if (code != 200) {
    nextMetricsMs = millis() + (retryAfter > 0 ? (uint32_t)retryAfter * 1000UL + 2000
                                               : METRICS_RETRY_MS);
    return;
  }

  // last occurrence of each banner header = latest day in range
  auto banner = [&](const char* header) -> String {
    int k = body.lastIndexOf(header);
    if (k < 0) return "";
    int dv = body.indexOf("\"display_value\"", k);
    if (dv < 0) return "";
    return jsonQuoted(body, "\"display_value\"", dv - 1);
  };
  metrics.soothes    = banner("\"SOOTHES\"");
  metrics.rise       = banner("\"RISE TIME\"");
  metrics.bed        = banner("\"BEDTIME\"");
  metrics.naps       = banner("\"NAPS\"");
  metrics.longest    = banner("\"LONGEST STRETCH\"");
  metrics.inBed      = banner("\"TIME IN BED\"");
  metrics.awakeInBed = banner("\"AWAKE IN BED\"");
  metrics.bedMin     = parseClockMin(metrics.bed);
  metrics.riseMin    = parseClockMin(metrics.rise);
  metrics.haveAny    = metrics.rise.length() || metrics.bed.length() || metrics.inBed.length();
  nextMetricsMs = millis() + METRICS_EVERY_MS;
}

// ---------------- derived state ----------------
DisplayState displayState() {
  switch (curStatus) {
    case BS_SLEEPING: {
      long asleep = (sinceEpoch && clockSynced()) ? (long)(time(nullptr) - sinceEpoch) : -1;
      return (asleep >= 0 && asleep < (long)SETTLED_SECS) ? DS_SETTLING : DS_ASLEEP;
    }
    case BS_AWAKE:    return DS_AWAKE;
    case BS_STIRRING: return DS_STIRRING;
    case BS_CRYING:   return DS_CRYING;
    case BS_AWAY:     return DS_AWAY;
    default:          return DS_BOOT;
  }
}

// bedtime .. rise time (falls back to 20:30 .. 07:00 until metrics arrive)
bool inNightWindow() {
  if (!clockSynced()) return false;
  int bed  = metrics.bedMin  >= 0 ? metrics.bedMin  : 20 * 60 + 30;
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
  st.tm_min  = bed % 60;
  st.tm_sec  = 0;
  st.tm_isdst = -1;
  time_t s = mktime(&st);
  if (s > now) s -= 86400;
  return s;
}

String fmtDur(long secs) {
  if (secs < 0) return "";
  if (secs < 60) return String(secs) + "s";
  if (secs < 3600) return String(secs / 60) + "m";
  return String(secs / 3600) + "h " + String((secs % 3600) / 60) + "m";
}

String clockStr(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  int h12 = lt.tm_hour % 12; if (h12 == 0) h12 = 12;
  char b[16];
  snprintf(b, sizeof(b), "%d:%02d %s", h12, lt.tm_min, lt.tm_hour < 12 ? "am" : "pm");
  return String(b);
}

// ---------------- UI ----------------
Preferences prefs;
M5Canvas canvas(&M5.Display);

const Box BOX_MUTE { 256,  0,  64, 48 };   // top-right corner, generous reach
const Box BOX_WORD {  40, 78, 240, 84 };   // the state name -> replay chime

// calm = settled-green with no recent touch, outside the night window
const uint8_t BRIGHT_DAY = 90, BRIGHT_CALM = 45, BRIGHT_ALERT = 255, BRIGHT_NIGHT = 12;
const uint8_t BRIGHT_LAMP = 50;               // lamp page: constant, never overridden
uint32_t nightWakeUntil = 0;                  // any touch holds full display this long

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
    canvas.drawLine(cx + (int)(cosf(a) * 10), cy + (int)(sinf(a) * 10),
                    cx + (int)(cosf(a) * 14), cy + (int)(sinf(a) * 14), fg);
  }
}
void drawBell(int cx, int cy, uint16_t fg) {
  canvas.fillArc(cx, cy + 3, 11, 0, 180, 360, fg);
  canvas.fillRect(cx - 12, cy + 3, 24, 3, fg);
  canvas.fillCircle(cx, cy + 9, 3, fg);
  canvas.fillCircle(cx, cy - 9, 2, fg);
}
void drawCrib(int cx, int cy, uint16_t fg) {
  for (int dx = -12; dx <= 12; dx += 6) canvas.drawFastVLine(cx + dx, cy - 10, 21, fg);
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
  const int xs[3] = { 280, 294, 308 };
  for (int i = 0; i < 3; ++i)
    canvas.fillCircle(xs[i], 222, 3, (int)page == i ? on : off);
}

// warning banner across the top; returns true if one was drawn
bool drawBanner() {
  String msg;
  if (tokenExpired)      msg = "Token expired - regenerate on dashboard";
  else if (subInactive)  msg = "Nurture Plus inactive - API paused";
  else if (WiFi.status() != WL_CONNECTED) msg = "Wi-Fi lost - reconnecting";
  else if (lastOkMs && millis() - lastOkMs > STALE_AFTER_MS)
    msg = "Last update " + fmtDur((millis() - lastOkMs) / 1000) + " ago";
  else return false;

  canvas.fillRect(0, 0, 320, 30, COL_BANNER);
  int tx = 24, ty = 15;
  canvas.fillTriangle(tx, ty - 7, tx - 8, ty + 7, tx + 8, ty + 7, COL_AMBER);
  canvas.fillTriangle(tx, ty - 4, tx - 5, ty + 5, tx + 5, ty + 5, COL_BANNER);
  canvas.drawFastVLine(tx, ty - 2, 4, COL_AMBER);
  canvas.drawPixel(tx, ty + 4, COL_AMBER);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_AMBER);
  canvas.drawString(msg, 40, 16);
  return true;
}

void drawStatusScreen() {
  DisplayState ds = displayState();

  uint16_t bg, fg;      // fg = full-strength text/icon color on bg
  const char* word;
  switch (ds) {
    case DS_ASLEEP:   bg = COL_GREEN;  fg = COL_WHITE;  word = "Asleep";      break;
    case DS_SETTLING: bg = COL_YELLOW; fg = COL_YELTX;  word = "Settling";    break;
    case DS_STIRRING: bg = COL_YELLOW; fg = COL_YELTX;  word = "Stirring";    break;
    case DS_AWAKE:    bg = COL_ORANGE; fg = COL_WHITE;  word = "Awake";       break;
    case DS_CRYING:   bg = COL_RED;    fg = COL_WHITE;  word = "Crying";      break;
    case DS_AWAY:     bg = COL_AWAYBG; fg = COL_AWAYTX; word = "Away";        break;
    default:          bg = COL_DARK;   fg = COL_WHITE;  word = "";            break;
  }
  uint16_t dim   = lerpCol(bg, fg, 0.75f);   // ~75% strength, like the mockups
  uint16_t faint = lerpCol(bg, fg, 0.55f);

  canvas.fillScreen(bg);

  // crying: expanding ring behind everything
  if (ds == DS_CRYING) {
    int r = 40 + (int)((millis() % 1100) * 110 / 1100);
    uint16_t ring = lerpCol(bg, COL_WHITE, 0.8f);
    for (int o = 0; o < 3; ++o) canvas.drawCircle(160, 120, r + o, ring);
  }

  // top: banner if something's wrong, else clock + mute toggle
  if (!drawBanner()) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(dim);
    if (clockSynced()) canvas.drawString(clockStr(time(nullptr)), 14, 17);
  }
  drawSpeakerIcon(290, 18, lerpCol(bg, fg, 0.75f), volIdx);

  // center: icon + state word + duration
  switch (ds) {
    case DS_ASLEEP:
    case DS_SETTLING: drawMoon(160, 72, dim, bg); break;
    case DS_STIRRING: drawWaves(160, 72, dim);    break;
    case DS_AWAKE:    drawSun(160, 72, dim);      break;
    case DS_CRYING:   drawBell(160, 72, fg);      break;
    case DS_AWAY:     drawCrib(160, 72, dim);     break;
    default: break;
  }
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextColor(fg);
  canvas.drawString(word, 160, 118);

  String sub;
  if (sinceEpoch && clockSynced()) {
    long secs = (long)(time(nullptr) - sinceEpoch);
    if (ds == DS_AWAY)          sub = "since " + clockStr(sinceEpoch);
    else if (ds == DS_SETTLING) sub = "asleep for " + fmtDur(secs);
    else                        sub = "for " + fmtDur(secs);
  }
  canvas.setFont(&fonts::FreeSans12pt7b);
  canvas.setTextColor(dim);
  canvas.drawString(sub, 160, 158);

  // crying: acknowledge pill
  if (ds == DS_CRYING) {
    const char* pill = cryAcked ? "chime silenced" : "tap to silence chime";
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

  canvas.pushSprite(0, 0);
}

void drawStatsScreen() {
  canvas.fillScreen(COL_DARK);

  if (!drawBanner()) {
    // battery, top-left: outline glyph with fill level + percentage
    int batt = M5.Power.getBatteryLevel();
    if (batt < 0) batt = 0;
    if (batt > 100) batt = 100;
    bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
    uint16_t bcol = (batt <= 20 && !charging) ? COL_AMBER : COL_DIM;
    canvas.drawRoundRect(14, 10, 24, 14, 3, bcol);
    canvas.fillRect(38, 14, 3, 6, bcol);                  // terminal nub
    canvas.fillRect(17, 13, batt * 18 / 100, 8, bcol);    // fill level
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
  drawSpeakerIcon(290, 18, COL_DIM, volIdx);

  const char* labels[6] = { "RISE TIME", "BEDTIME", "LONGEST", "NAPS", "SOOTHES", "AWAKE IN BED" };
  String vals[6] = { metrics.rise, metrics.bed, metrics.longest,
                     metrics.naps, metrics.soothes, metrics.awakeInBed };
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

  canvas.pushSprite(0, 0);
}

void drawNightScreen() {
  canvas.fillScreen(COL_BLACK);
  const float glow[4] = { 0.05f, 0.09f, 0.14f, 0.22f };
  const int   rad[4]  = { 78, 58, 40, 24 };
  for (int i = 0; i < 4; ++i)
    canvas.fillCircle(160, 104, rad[i], lerpCol(COL_BLACK, COL_GREEN, glow[i]));
  canvas.fillCircle(160, 104, 6, COL_NIGHTDOT);
  String sub = "Asleep";
  if (sinceEpoch && clockSynced()) sub += "  " + fmtDur((long)(time(nullptr) - sinceEpoch));
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_NIGHT);
  canvas.drawString(sub, 160, 168);
  canvas.pushSprite(0, 0);
}

// score -> color: >=85% is a great baby night (deep green); the band slides
// through amber down to a soft ember at 50%, and stays ember below — no
// alarm-red about a hard night someone is already living
uint16_t scoreColor(float p) {
  const uint16_t GOODC = rgb565(18, 160, 88);
  const uint16_t MIDC  = rgb565(227, 168, 11);
  const uint16_t LOWC  = rgb565(200, 72, 28);
  if (p >= 0.85f)  return GOODC;
  if (p >= 0.675f) return lerpCol(MIDC, GOODC, (p - 0.675f) / 0.175f);
  if (p >= 0.50f)  return lerpCol(LOWC, MIDC, (p - 0.50f) / 0.175f);
  return LOWC;
}

void drawLampScreen() {
  uint32_t tot = nsGoodSecs + nsBadSecs;
  bool haveScore = nightActive && tot >= 45 * 60;   // small denominators swing wildly
  uint16_t col = haveScore ? scoreColor((float)nsGoodSecs / (float)tot)
                           : rgb565(64, 74, 100);   // neutral: night not underway yet

  canvas.fillScreen(COL_BLACK);
  const float glow[5] = { 0.10f, 0.18f, 0.30f, 0.48f, 0.80f };
  const int   rad[5]  = { 150, 120, 95, 72, 50 };
  for (int i = 0; i < 5; ++i)
    canvas.fillCircle(160, 120, rad[i], lerpCol(COL_BLACK, col, glow[i]));

  if ((int32_t)(millis() - lampPeekUntil) < 0) {    // tap-to-peek overlay
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_WHITE);
    if (haveScore) {
      int pct = (int)(100.0f * nsGoodSecs / tot + 0.5f);
      canvas.setFont(&fonts::FreeSansBold24pt7b);
      canvas.drawString(String(pct) + "%", 160, 96);
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.drawString("asleep " + fmtDur((long)nsGoodSecs) + "   awake " + fmtDur((long)nsBadSecs), 160, 148);
      canvas.drawString(String(nsWakings) + (nsWakings == 1 ? " waking" : " wakings"), 160, 172);
    } else {
      canvas.setFont(&fonts::FreeSans12pt7b);
      canvas.drawString(nightActive ? String("night just started")
                                    : "night starts " + (metrics.bed.length() ? metrics.bed : String("8:30 pm")),
                        160, 120);
    }
  }
  canvas.pushSprite(0, 0);
}

void drawBootScreen(const char* msg) {
  canvas.fillScreen(COL_DARK);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(COL_WHITE);
  canvas.drawString("CradleWatch", 160, 96);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(msg, 160, 140);
  canvas.pushSprite(0, 0);
}

// ---------------- Wi-Fi ----------------
uint32_t wifiRetryMs = 0;
void serviceWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if ((int32_t)(millis() - wifiRetryMs) < 0) return;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiRetryMs = millis() + 30000;
}

// ---------------- touch ----------------
void advancePage() {
  page = (Page)(((int)page + 1) % 3);         // status -> stats -> lamp -> status
  prefs.putInt("page", (int)page);
}

void handleTap(int x, int y) {
  if (nightDimActive) {                       // first tap just wakes the screen
    nightWakeUntil = millis() + 15000;
    return;
  }
  nightWakeUntil = millis() + 15000;

  if (page == PAGE_LAMP) {                    // first tap peeks; tap again moves on
    if ((int32_t)(millis() - lampPeekUntil) < 0) advancePage();
    else lampPeekUntil = millis() + 10000;
    buzz(15);
    return;
  }
  if (BOX_MUTE.hit(x, y)) {
    volIdx = (volIdx + 1) % 4;                // mute -> low -> med -> high -> mute
    prefs.putInt("vol", volIdx);
    if (isMuted()) {
      stopSong();
    } else {                                  // audible blip at the new level
      M5.Speaker.setVolume(currentVolume());
      M5.Speaker.tone(440, 120);
    }
    buzz(25);
    return;
  }
  if (curStatus == BS_CRYING && !cryAcked) {  // any tap silences the cry loop
    cryAcked = true;
    stopSong();
    buzz(25);
    return;
  }
  if (page == PAGE_STATUS && BOX_WORD.hit(x, y) && curStatus != BS_NONE) {
    playSong(songForStatus(curStatus));       // replay the state's chime
    buzz(15);                                 // tactile ack, even when muted
    return;
  }
  advancePage();
  buzz(15);
}

// ---------------- arduino entry points ----------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(BRIGHT_DAY);
  M5.Speaker.begin();

  // power: 80 MHz is the Wi-Fi floor and plenty for this workload; the side
  // power LED has no place in a nursery anyway
  setCpuFrequencyMhz(80);
  M5.Power.setLed(0);

  prefs.begin("cradlewatch", false);
  volIdx = prefs.getInt("vol", prefs.getBool("muted", false) ? 0 : 2);  // migrate old mute pref
  if (volIdx < 0 || volIdx > 3) volIdx = 2;
  int pg = prefs.getInt("page", 0);
  page = (pg >= 0 && pg <= 2) ? (Page)pg : PAGE_STATUS;

  canvas.setColorDepth(16);
  if (!canvas.createSprite(320, 240)) {
    canvas.setPsram(true);
    canvas.createSprite(320, 240);
  }

  drawBootScreen("connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);                        // modem sleep: the biggest battery lever
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(200);
  wifiRetryMs = millis() + 30000;

  configTzTime(TZ_STRING, "pool.ntp.org", "time.nist.gov");
  drawBootScreen(WiFi.status() == WL_CONNECTED ? "asking the crib..." : "Wi-Fi failed - retrying");
}

void loop() {
  M5.update();
  serviceVibe();
  serviceSong();
  serviceWifi();

  // night-score tally: one counted second at a time, only with fresh data
  static uint32_t nextAccumMs = 0;
  if (clockSynced() && (int32_t)(millis() - nextAccumMs) >= 0) {
    nextAccumMs = millis() + 1000;
    if (inNightWindow()) {
      if (!nightActive) {
        nightActive = true;
        nsGoodSecs = nsBadSecs = 0;
        nsWakings = 0;
        nsBackfillTried = false;
        nightStartEpoch = nightWindowStart();
      }
      bool fresh = lastOkMs && (millis() - lastOkMs) < STALE_AFTER_MS;
      if (fresh && curStatus != BS_NONE) {
        if (curStatus == BS_SLEEPING || curStatus == BS_STIRRING) nsGoodSecs++;
        else nsBadSecs++;
      }
    } else {
      nightActive = false;
    }
  }

  // polls (blocking for ~1s; deferred while a chime plays so notes stay even)
  if (playingSong < 0) {
    if ((int32_t)(millis() - nextStatusMs) >= 0) pollStatus();
    else if ((int32_t)(millis() - nextMetricsMs) >= 0) pollDayMetrics();
    else if (nightActive && !nsBackfillTried && clockSynced() &&
             time(nullptr) - nightStartEpoch > 600 &&
             nsGoodSecs + nsBadSecs < 120) {   // rebooted mid-night: catch up once
      nsBackfillTried = true;
      backfillNightScore();
    }
  }

  // crying chime loop (silent on the lamp page: the on-shift parent has it)
  if (curStatus == BS_CRYING && !isMuted() && !cryAcked && playingSong < 0 &&
      page != PAGE_LAMP &&
      (int32_t)(millis() - cryNextLoopMs) >= 0) {
    playSong(SONG_STORMS);
    cryNextLoopMs = millis() + 5500;
  }

  // touch + buttons — a tap forces an immediate redraw for instant feedback
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) { handleTap(t.x, t.y); nextDrawMs = 0; }
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
    nightWakeUntil = millis() + 15000;
    if (!nightDimActive) advancePage();
    nextDrawMs = 0;
  }

  // night dimming: settled sleep, inside the sleep window, no recent touch
  DisplayState ds = displayState();
  nightDimActive = (ds == DS_ASLEEP) && inNightWindow() && page == PAGE_STATUS &&
                   (int32_t)(millis() - nightWakeUntil) >= 0 &&
                   !tokenExpired && !subInactive;

  if (page == PAGE_LAMP) {                    // the lamp is constant, always
    nightDimActive = false;
    M5.Display.setBrightness(BRIGHT_LAMP);
  } else if (ds == DS_CRYING) {
    nightDimActive = false;
    M5.Display.setBrightness(BRIGHT_ALERT);
  } else if (nightDimActive) {
    M5.Display.setBrightness(BRIGHT_NIGHT);
  } else if (ds == DS_ASLEEP && page == PAGE_STATUS &&
             (int32_t)(millis() - nightWakeUntil) >= 0) {
    M5.Display.setBrightness(BRIGHT_CALM);    // settled green, nobody watching
  } else {
    M5.Display.setBrightness(BRIGHT_DAY);
  }

  // drawing is throttled to ~12 fps, but the loop itself spins every ~5 ms so
  // M5.update() samples touch often enough that short taps always register.
  // A full sprite push takes ~30 ms, so hold the redraw when a chime note is
  // about to start — otherwise the draw stall smears the rhythm.
  bool noteSoon = playingSong >= 0 && (int32_t)(noteNextMs - millis()) < 40;
  if (!noteSoon && (int32_t)(millis() - nextDrawMs) >= 0) {
    if (curStatus == BS_NONE) {
      if (tokenExpired)          drawBootScreen("token expired - regenerate on dashboard");
      else if (subInactive)      drawBootScreen("Nurture Plus inactive");
      else if (WiFi.status() != WL_CONNECTED) drawBootScreen("Wi-Fi failed - retrying");
      else                       drawBootScreen("asking the crib...");
    } else if (page == PAGE_LAMP) drawLampScreen();
    else if (nightDimActive)     drawNightScreen();
    else if (page == PAGE_STATS) drawStatsScreen();
    else                         drawStatusScreen();
    // 1 fps idle (only the duration text changes); ~12 fps for the crying ring
    nextDrawMs = millis() + (ds == DS_CRYING ? 80 : 1000);
  }

  delay(5);
}
