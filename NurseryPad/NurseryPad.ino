/* NurseryPad protocol 1. UI runs on Arduino's core; HTTPS runs on core 0.
 * Pair in the web app. No Cradlewise token belongs on this device.
 */
#include "cradlewatch_types.h"
#include "pad_types.h"
#include "secrets.h"
#include "supabase_certs.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <time.h>

volatile bool ntpSynced = false;
time_t sourceObserved = 0, snapshotEpoch = 0;
uint32_t snapshotMs = 0;
String sourceError;
long sourceAge() {
  return sourceObserved && snapshotEpoch ? max(0L, (long)(snapshotEpoch - sourceObserved) +
                                                       (long)((millis() - snapshotMs) / 1000))
                                         : 1000000;
}
#include "dashboard.h"

Screen screen = HUB;
QueueHandle_t commands, results;
DynamicJsonDocument snapshot(16384), sleepStats(16384);
String outbox[8], timerPending, caregiver, message, lastResultRow;
// A one-shot log the server refused for good. Held for review; never blocks the queue.
String rejected, rejectedReason;
int queued = 0, amount = 60, pumpAmount = 0, retryStep = 0;
// After a timer op is applied, Switch/Stop stay disabled until a newer snapshot has arrived,
// so a second tap can never carry a stale expected_updated_at.
bool awaitSnapshot = false;
bool storageReady = true, bottleDraft = false;
bool wet = true, dirty = false, formula = true, pending = false, failed = false,
     statsWanted = false, bootApplied = false;
uint32_t lastTouch = 0, nextPoll = 0, retryAt = 0, touchAt = 0, toastUntil = 0;
int touchX = 0, touchY = 0;

String uuid() {
  uint8_t b[16];
  esp_fill_random(b, sizeof(b));
  b[6] = (b[6] & 15) | 64;
  b[8] = (b[8] & 63) | 128;
  char s[37];
  snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13],
           b[14], b[15]);
  return s;
}
uint32_t backoffMs() {
  static const uint32_t steps[] = {15000, 30000, 60000, 300000};
  uint32_t ms = steps[min(retryStep, 3)];
  retryStep = min(retryStep + 1, 3);
  return ms;
}
String isoNow() {
  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  char s[32];
  strftime(s, sizeof(s), "%Y-%m-%dT%H:%M:%SZ", &t);
  return s;
}
bool live() {
  return !failed && snapshotEpoch && millis() - snapshotMs < 45000 &&
         WiFi.status() == WL_CONNECTED && ntpSynced;
}
void tell(String text, bool error = false) {
  message = text;
  failed = error;
  toastUntil = error ? 0 : millis() + 2500;
  nextDrawMs = 0;
}
bool persistOutbox() {
  DynamicJsonDocument doc(16384);
  JsonArray a = doc.to<JsonArray>();
  for (int i = 0; i < queued; i++)
    a.add(outbox[i]);
  String data;
  serializeJson(doc, data);
  return prefs.putString("outbox", data) == data.length();
}
void loadOutbox() {
  DynamicJsonDocument doc(16384);
  String raw = prefs.getString("outbox", "[]");
  if (deserializeJson(doc, raw) || !doc.is<JsonArray>()) {
    storageReady = false;
    tell("Stored queue needs recovery", true);
    return;
  }
  for (JsonVariant v : doc.as<JsonArray>()) {
    if (queued < 8 && v.is<const char *>() && v.as<String>().length() < 1536)
      outbox[queued++] = v.as<String>();
    else {
      storageReady = false;
      tell("Stored queue needs recovery", true);
      return;
    }
  }
  timerPending = prefs.getString("timer", "");
}
void navigate(Screen next) {
  screen = next;
  lastTouch = millis();
  nightWakeUntil = millis() + 15000;
  nextDrawMs = 0;
  if (next == SLEEP_LOG || next == SLEEP_STATS)
    statsWanted = true;
}
void request(const char *rpc, const String &body, bool op = false) {
  if (body.length() >= 1536) {
    tell("Request too large", true);
    return;
  }
  NetCommand c = {};
  strlcpy(c.rpc, rpc, sizeof(c.rpc));
  strlcpy(c.body, body.c_str(), sizeof(c.body));
  c.operation = op;
  if (xQueueSend(commands, &c, 0) == pdTRUE)
    pending = true;
}
void netTask(void *) {
  NetCommand cmd;
  for (;;) {
    if (xQueueReceive(commands, &cmd, portMAX_DELAY) != pdTRUE)
      continue;
    NetResult result = {-1, cmd.operation, String(cmd.rpc) == "device_sleep_today", nullptr};
    for (int ca = 0; ca < 2; ca++) {
      if (WiFi.status() != WL_CONNECTED)
        break;
      WiFiClientSecure client;
      client.setCACert(ca == 0 ? GTS_ROOT_R4 : ISRG_ROOT_X1);
      client.setHandshakeTimeout(8);
      HTTPClient http;
      http.useHTTP10(true);
      http.setConnectTimeout(5000);
      http.setTimeout(6000);
      if (!http.begin(client, String(SUPABASE_URL) + "/rest/v1/rpc/" + cmd.rpc))
        continue;
      http.addHeader("apikey", SUPABASE_ANON_KEY);
      http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
      http.addHeader("x-device-key", DEVICE_KEY);
      http.addHeader("Content-Type", "application/json");
      result.code = http.POST((uint8_t *)cmd.body, strlen(cmd.body));
      if (result.code > 0) {
        // Bound response memory even when an endpoint is misconfigured.
        int size = http.getSize();
        if (size > 24000) {
          result.code = -2;
          http.end();
          break;
        }
        String body;
        body.reserve(8192);
        WiFiClient *stream = http.getStreamPtr();
        uint32_t deadline = millis() + 6000;
        while (http.connected() && (size > 0 || size == -1) && (int32_t)(deadline - millis()) > 0) {
          while (stream->available()) {
            char c = stream->read();
            body += c;
            if (size > 0)
              size--;
            if (body.length() > 24000)
              break;
          }
          if (body.length() > 24000 || size == 0)
            break;
          vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (body.length() > 24000)
          result.code = -2;
        else
          result.body = strdup(body.c_str());
        http.end();
        break;
      }
      http.end();
    }
    if (xQueueSend(results, &result, portMAX_DELAY) != pdTRUE)
      free(result.body);
  }
}
void submit(const char *op, JsonObject args, JsonObject target = JsonObject()) {
  if (!storageReady) {
    tell("Storage needs recovery; not saved", true);
    return;
  }
  bool oneshot = String(op) == "bottle" || String(op) == "diaper";
  if (!oneshot && !ntpSynced) {
    tell("Waiting for clock sync", true);
    return;
  }
  if (caregiver.isEmpty()) {
    tell("Select a caregiver first", true);
    return;
  }
  if (!oneshot && (!live() || timerPending.length() || awaitSnapshot)) {
    tell("Wait for sync before timers", true);
    return;
  }
  if (oneshot && queued == 8) {
    tell("Queue full (8). Connect to sync", true);
    return;
  }
  DynamicJsonDocument doc(1536);
  doc["protocol"] = 1;
  doc["client_op_id"] = uuid();
  doc["op"] = op;
  // Without NTP the server timestamps a one-shot log at receipt and marks it time_uncertain.
  doc["occurred_at"] = isoNow();
  doc["clock_ok"] = ntpSynced;
  doc["caregiver_id"] = caregiver;
  doc["args"] = args;
  if (!target.isNull()) {
    doc["target_id"] = target["id"];
    doc["expected_updated_at"] = target["updated_at"];
  }
  String body;
  serializeJson(doc, body);
  if (oneshot) {
    outbox[queued++] = body;
    if (!persistOutbox()) {
      queued--;
      tell("Storage failed. Not saved", true);
      return;
    }
    tell("Saved locally - pending sync");
    if (String(op) == "bottle")
      bottleDraft = false;
    navigate(HUB);
  } else {
    if (prefs.putString("timer", body) != body.length()) {
      tell("Storage failed. Not sent", true);
      return;
    }
    timerPending = body;
    tell("Pending...");
  }
  retryAt = 0;
}
void action(const char *op, const char *targetName = nullptr) {
  DynamicJsonDocument args(256);
  JsonObject a = args.to<JsonObject>();
  if (String(op) == "sleep_start")
    a["place"] = "crib";
  submit(op, a, targetName ? snapshot[targetName].as<JsonObject>() : JsonObject());
}
long elapsed(JsonObject timer) {
  if (timer.isNull())
    return 0;
  if (timer.containsKey("elapsed_s"))
    return timer["elapsed_s"].as<long>() + (millis() - snapshotMs) / 1000;
  return max(0L, (long)(snapshotEpoch - parseIso8601Utc(timer["open_since"] | "")) +
                     (long)((millis() - snapshotMs) / 1000));
}
void dropHead() {
  for (int i = 1; i < queued; i++)
    outbox[i - 1] = outbox[i];
  if (queued)
    outbox[--queued] = "";
  if (!persistOutbox())
    tell("Saved; queue storage needs repair", true);
}
/** The server refused the oldest one-shot log for good: park it for review so the logs behind it
 *  still flush, and say why. */
void rejectHead(const String &reason) {
  if (!queued)
    return;
  rejected = outbox[0];
  rejectedReason = reason;
  dropHead();
  tell("Log rejected. Tap to review", true);
}
void consume(NetResult &r) {
  pending = false;
  if (r.code != 200 || !r.body) {
    // 401/403 on any call means the pairing is gone. Another 4xx on an op is the server's
    // final word on that envelope; everything else (5xx, transport, oversize) is retried
    // with backoff, never left wedged.
    if (r.code == 401 || r.code == 403) {
      tell("Device rejected - check pairing", true);
      retryAt = millis() + backoffMs();
    } else if (r.operation && r.code >= 400 && r.code < 500) {
      if (timerPending.length()) {
        tell("Timer rejected. Tap to review", true);
        retryAt = UINT32_MAX;
      } else {
        rejectHead("http_" + String(r.code));
        retryAt = 0;
      }
    } else {
      tell("Connection failed - retrying", true);
      retryAt = millis() + backoffMs();
    }
    free(r.body);
    return;
  }
  DynamicJsonDocument doc(24576);
  // A const char* makes ArduinoJson copy the strings; the buffer is freed right after.
  auto error = deserializeJson(doc, (const char *)r.body);
  free(r.body);
  r.body = nullptr;
  if (error || !doc.is<JsonObject>()) {
    tell("Invalid response - retrying", true);
    retryAt = millis() + backoffMs();
    return;
  }
  retryStep = 0;
  if (r.operation) {
    const String outcome = doc["outcome"] | "";
    if (outcome == "applied" || outcome == "duplicate") {
      DynamicJsonDocument sent(1536);
      deserializeJson(sent, timerPending.length() ? timerPending : outbox[0]);
      String op = sent["op"] | "";
      if (timerPending.length()) {
        prefs.remove("timer");
        timerPending = "";
        awaitSnapshot = true;
      } else
        dropHead();
      if (op == "bf_stop") {
        lastResultRow = "";
        serializeJson(doc["row"], lastResultRow);
        navigate(FEED_DONE);
      } else if (op == "pump_stop") {
        lastResultRow = "";
        serializeJson(doc["row"], lastResultRow);
        navigate(PUMP_AMOUNT);
      } else if (op == "pump_amount") {
        navigate(HUB);
      } else if (op == "bf_start" || op == "bf_switch")
        navigate(FEED_TIMER);
      else if (op == "pump_start")
        navigate(PUMP_TIMER);
      else if (op == "delete")
        navigate(HUB);
      if (op == "sleep_start" || op == "sleep_stop" || op == "sleep_dismiss")
        statsWanted = true;
      tell("Saved");
      nextPoll = 0;
    } else if (outcome == "retry" || outcome.isEmpty()) {
      tell("Server busy - retrying", true);
      retryAt = millis() + backoffMs();
    } else if (timerPending.length()) {
      // A timer rejection is retained until explicitly acknowledged; nothing else is queued
      // behind it because timers are never submitted while one is pending.
      tell(outcome == "conflict" ? "Changed elsewhere. Tap to refresh"
                                 : "Log rejected. Tap to review",
           true);
      retryAt = UINT32_MAX;
    } else {
      rejectHead(outcome + (doc["reason"].is<const char *>() ? ": " + String(doc["reason"].as<const char *>()) : ""));
      retryAt = 0;
    }
    return;
  }
  if (r.stats) {
    sleepStats.set(doc);
    return;
  }
  if (!doc["protocol"].is<int>() || doc["protocol"].as<int>() != 1 ||
      !doc["snapshot_at"].is<const char *>() || !doc["caregivers"].is<JsonArray>()) {
    tell("Update firmware: protocol mismatch", true);
    return;
  }
  for (const char *name : {"bf", "pump", "sleep"})
    if (!doc[name].isNull() &&
        (!doc[name].is<JsonObject>() || !doc[name]["id"].is<const char *>() ||
         !doc[name]["updated_at"].is<const char *>() ||
         !doc[name]["open_since"].is<const char *>())) {
      tell("Invalid timer data", true);
      return;
    }
  snapshot.set(doc);
  snapshotEpoch = parseIso8601Utc(doc["snapshot_at"] | "");
  snapshotMs = millis();
  sourceObserved = parseIso8601Utc(doc["source_observed_at"] | "");
  sourceError = doc["source_error"] | "";
  sinceEpoch = parseIso8601Utc(doc["source_since"] | "");
  cribBounce = String(doc["source_bounce"] | "") == "on";
  cribMusic = String(doc["source_music"] | "") == "on";
  String status = doc["source_status"] | "unknown";
  BabyStatus next = status == "sleeping"   ? BS_SLEEPING
                    : status == "awake"    ? BS_AWAKE
                    : status == "stirring" ? BS_STIRRING
                    : status == "crying"   ? BS_CRYING
                    : status == "away"     ? BS_AWAY
                                           : BS_NONE;
  if (next != curStatus) {
    curStatus = next;
    if (next == BS_CRYING) {
      cryAcked = false;
      cryNextLoopMs = 0;
    }
    if (!firstStatus && page != PAGE_LAMP && sourceAge() < 180 && !sourceError.length())
      playSong(songForStatus(next));
  }
  firstStatus = false;
  metrics.bedMin = doc["bed_min"] | 1200;
  metrics.riseMin = doc["rise_min"] | 480;
  metrics.bed = String(metrics.bedMin / 60) + ":" + (metrics.bedMin % 60 < 10 ? "0" : "") +
                String(metrics.bedMin % 60);
  metrics.rise = String(metrics.riseMin / 60) + ":" + (metrics.riseMin % 60 < 10 ? "0" : "") +
                 String(metrics.riseMin % 60);
  nightActive = !doc["settled_pct"].isNull();
  nsGoodSecs = (doc["settled_asleep_min"] | 0.0) * 60;
  nsBadSecs = (doc["settled_awake_min"] | 0.0) * 60;
  nsWakings = doc["settled_wakings"] | 0;
  bool found = false;
  for (JsonObject c : doc["caregivers"].as<JsonArray>())
    if (c["id"].as<String>() == caregiver)
      found = true;
  if (!found) {
    caregiver = doc["caregivers"][0]["id"] | "";
    prefs.putString("caregiver", caregiver);
  }
  String boot = doc["boot_mode"] | "hub";
  if (boot != prefs.getString("serverBoot", "")) {
    prefs.putString("serverBoot", boot);
    prefs.putString("boot", boot);
    navigate(boot == "hub" ? HUB : DASHBOARD);
    page = boot == "lamp" ? PAGE_LAMP : PAGE_STATUS;
  }
  bootApplied = true;
  awaitSnapshot = false;
  // A rejected operation keeps its warning until the caregiver reviews it.
  if (retryAt != UINT32_MAX) {
    failed = false;
    if (!queued && !timerPending.length() && !rejected.length())
      message = "";
  }
  nextDrawMs = 0;
}
PadButton buttons[24];
int buttonCount = 0;
bool reviewing = false;
void button(int x, int y, int w, int h, const String &label, int action, bool enabled = true) {
  canvas.fillRoundRect(x, y, w, h, 8, enabled ? COL_PANEL : COL_DARK);
  canvas.drawRoundRect(x, y, w, h, 8, enabled ? COL_DIM : COL_FAINT);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(enabled ? COL_WHITE : COL_FAINT);
  canvas.drawString(label, x + w / 2, y + h / 2);
  if (buttonCount < 24)
    buttons[buttonCount++] = {x, y, w, h, action, enabled};
}
void line(const String &text, int y, uint16_t color = COL_WHITE) {
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(color);
  canvas.drawString(text, 160, y);
}
const char *timerName() {
  if (!snapshot["bf"].isNull() && screen != FEED_TIMER)
    return "bf";
  if (!snapshot["pump"].isNull() && screen != PUMP_TIMER && screen != PUMP_AMOUNT)
    return "pump";
  if (!snapshot["sleep"].isNull() && screen != SLEEP_LOG)
    return "sleep";
  return nullptr;
}
void drawPad() {
  buttonCount = 0;
  canvas.fillScreen(COL_DARK);
  button(2, 2, 32, 32, "<", 1);
  button(282, 2, 36, 32, sourceAge() > 900 ? "?" : "o", 2);
  String who = "Caregiver";
  for (JsonObject c : snapshot["caregivers"].as<JsonArray>())
    if (c["id"].as<String>() == caregiver)
      who = c["name"].as<String>();
  button(177, 2, 100, 32, who.substring(0, 10), 3);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_WHITE);
  canvas.drawString(ntpSynced ? clockStr(time(nullptr)) : "Syncing clock", 40, 18);
  int y = 40;
  const char *timer = timerName();
  if (timer) {
    button(4, 38, 312, 32, String(timer) + "  " + fmtDur(elapsed(snapshot[timer].as<JsonObject>())),
           4);
    y = 76;
  }
  const int w = 150, h = 40, gap = 45;
  bool enabled = live() && !timerPending.length() && !awaitSnapshot;
  if (reviewing) {
    line(rejected.length() ? "A log was refused: " + rejectedReason.substring(0, 22)
                           : "Operation needs review",
         y + 12, COL_AMBER);
    line(rejected.length() ? "It was not saved. Log it again if needed."
                           : "Refresh to see the latest log.",
         y + 40);
    button(8, y + 58, 146, 44, "Refresh", 90);
    button(166, y + 58, 146, 44, "Retry same op", 91, !rejected.length());
    button(8, y + 110, 304, 40, rejected.length() ? "Discard refused log" : "Acknowledge timer rejection", 92);
  } else
    switch (screen) {
    case HUB:
      button(6, y, w, h, "Feed", 10);
      button(164, y, w, h, "Bottle", 11);
      button(6, y + gap, w, h, "Change", 12);
      button(164, y + gap, w, h, "Pump", 13);
      button(6, y + gap * 2, w, h, "Sleep log", 14);
      button(164, y + gap * 2, w, h, "Sleep stats", 15);
      break;
    case FEED:
      line("Start breastfeed", y + 18);
      button(8, y + 40, 146, 52, "Left", 20, enabled);
      button(166, y + 40, 146, 52, "Right", 21, enabled);
      if (!enabled)
        line("Offline / waiting for sync", y + 115, COL_AMBER);
      break;
    case FEED_TIMER:
      line("Feeding " + String(snapshot["bf"]["side"] | ""), y + 15);
      line(fmtDur(elapsed(snapshot["bf"].as<JsonObject>())), y + 45);
      button(8, y + 65, 146, 48, "Switch side", 22, enabled && !snapshot["bf"].isNull());
      button(166, y + 65, 146, 48, "Stop", 23, enabled && !snapshot["bf"].isNull());
      break;
    case FEED_DONE: {
      DynamicJsonDocument done(2048);
      deserializeJson(done, lastResultRow);
      long l = done["payload"]["left_s"] | 0, r = done["payload"]["right_s"] | 0;
      line("Feed saved", y + 15);
      line("L " + fmtDur(l) + "   R " + fmtDur(r), y + 45);
      button(8, y + 65, 146, 48, "Delete", 24, enabled);
      button(166, y + 65, 146, 48, "Done", 25);
      break;
    }
    case BOTTLE:
      line(String(amount) + " mL", y + 15);
      button(8, y + 35, 60, 40, "-10", 30);
      button(252, y + 35, 60, 40, "+10", 31);
      button(76, y + 35, 168, 40, formula ? "Formula" : "Breast milk", 32);
      button(8, y + 90, 304, 48, "Log bottle", 33, ntpSynced && queued < 8);
      break;
    case DIAPER_SCREEN:
      button(8, y + 8, 146, 48, wet ? "Wet: yes" : "Wet: no", 40);
      button(166, y + 8, 146, 48, dirty ? "Dirty: yes" : "Dirty: no", 41);
      button(8, y + 75, 304, 48, "Log change", 42, ntpSynced && (wet || dirty) && queued < 8);
      break;
    case PUMP:
      line("Pump", y + 20);
      button(8, y + 55, 304, 52, "Start pump", 50, enabled);
      break;
    case PUMP_TIMER:
      line("Pumping " + fmtDur(elapsed(snapshot["pump"].as<JsonObject>())), y + 25);
      button(8, y + 65, 304, 52, "Stop pump", 51, enabled && !snapshot["pump"].isNull());
      break;
    case PUMP_AMOUNT:
      line("Total " + String(pumpAmount) + " mL", y + 18);
      button(8, y + 40, 146, 40, "-10", 52);
      button(166, y + 40, 146, 40, "+10", 53);
      button(8, y + 90, 304, 48, "Save amount", 54, enabled);
      break;
    case SLEEP_LOG:
      if (snapshot["sleep"].isNull())
        button(8, y, 146, 40, "Start nap", 60, enabled);
      else
        button(8, y, 146, 40,
               String(snapshot["sleep"]["source"] | "") == "cradlewise" ? "End sleep"
                                                                        : "Stop sleep",
               61, enabled);
      if (!snapshot["sleep"].isNull() && String(snapshot["sleep"]["source"] | "") == "cradlewise")
        button(166, y, 146, 40, "Not a nap", 62, enabled);
      else
        button(166, y, 146, 40, "Stats", 15);
      {
        int i = 0;
        for (JsonObject e : sleepStats["rows"].as<JsonArray>()) {
          if (i++ >= 3)
            break;
          line(String(e["kind"] | "sleep") + " " + fmtDur(e["duration_s"] | 0L) + " " +
                   String(e["place"] | ""),
               y + 45 + i * 22, COL_DIM);
        }
      }
      break;
    case SLEEP_STATS: {
      String labels[] = {"Sleep", "Naps", "Longest nap", "Last night", "Wakings", "Awake in bed"};
      String values[] = {fmtDur((sleepStats["sleep_min"] | 0.0) * 60),
                         String(sleepStats["naps"] | 0),
                         fmtDur((sleepStats["longest_nap_min"] | 0.0) * 60),
                         fmtDur((sleepStats["last_night_min"] | 0.0) * 60),
                         String(sleepStats["last_night_wakings"] | 0),
                         sleepStats["awake_in_bed_s"].isNull()
                             ? "--"
                             : fmtDur(sleepStats["awake_in_bed_s"].as<long>())};
      for (int i = 0; i < 6; i++) {
        int x = i % 2 * 158 + 5, yy = y + i / 2 * 45;
        canvas.fillRoundRect(x, yy, 152, 40, 6, COL_PANEL);
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(top_left);
        canvas.setTextColor(COL_DIM);
        canvas.drawString(labels[i], x + 6, yy + 3);
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextColor(COL_WHITE);
        canvas.drawString(values[i], x + 6, yy + 15);
      }
      break;
    }
    default:
      break;
    }
  if (message.length() && (failed || !toastUntil || (int32_t)(toastUntil - millis()) > 0)) {
    canvas.fillRect(0, 216, 320, 24, COL_BANNER);
    line(message.substring(0, 39), 228, failed ? COL_AMBER : COL_WHITE);
  } else if (sourceAge() > 180 || sourceError.length()) {
    canvas.fillRect(0, 216, 320, 24, COL_BANNER);
    line(sourceError.length() ? "Crib: " + sourceError
         : sourceObserved     ? "Crib data " + String(sourceAge() / 60) + " min old"
                              : "Crib data unavailable",
         228, COL_AMBER);
  } else if (screen == SLEEP_STATS)
    line("Rise " + metrics.rise + "  Bed " + metrics.bed, 229, COL_DIM);
  canvas.pushSprite(0, 0);
}
void doAction(int a) {
  DynamicJsonDocument args(256);
  JsonObject obj = args.to<JsonObject>();
  switch (a) {
  case 1:
    navigate(HUB);
    break;
  case 2:
    navigate(DASHBOARD);
    page = PAGE_STATUS;
    break;
  case 3: {
    JsonArray cs = snapshot["caregivers"];
    for (size_t i = 0; i < cs.size(); i++)
      if (cs[i]["id"].as<String>() == caregiver) {
        caregiver = cs[(i + 1) % cs.size()]["id"].as<String>();
        prefs.putString("caregiver", caregiver);
        break;
      }
    break;
  }
  case 4: {
    String timer = timerName();
    navigate(timer == "bf" ? FEED_TIMER : timer == "pump" ? PUMP_TIMER : SLEEP_LOG);
    break;
  }
  case 10:
    navigate(snapshot["bf"].isNull() ? FEED : FEED_TIMER);
    break;
  case 11:
    if (!bottleDraft) {
      amount = constrain(snapshot["bottle_default_ml"] | 60, 1, 1000);
      bottleDraft = true;
    }
    navigate(BOTTLE);
    break;
  case 12:
    navigate(DIAPER_SCREEN);
    break;
  case 13:
    navigate(snapshot["pump"].isNull() ? PUMP : PUMP_TIMER);
    break;
  case 14:
    navigate(SLEEP_LOG);
    break;
  case 15:
    navigate(SLEEP_STATS);
    break;
  case 20:
  case 21:
    obj["side"] = a == 20 ? "left" : "right";
    submit("bf_start", obj);
    break;
  case 22:
    action("bf_switch", "bf");
    break;
  case 23:
    action("bf_stop", "bf");
    break;
  case 24: {
    DynamicJsonDocument e(2048);
    deserializeJson(e, lastResultRow);
    submit("delete", obj, e.as<JsonObject>());
    break;
  }
  case 25:
    navigate(HUB);
    break;
  case 30:
    amount = max(1, amount - 10);
    break;
  case 31:
    amount = min(1000, amount + 10);
    break;
  case 32:
    formula = !formula;
    break;
  case 33:
    obj["ml"] = amount;
    obj["kind"] = formula ? "formula" : "breast_milk";
    submit("bottle", obj);
    break;
  case 40:
    wet = !wet;
    break;
  case 41:
    dirty = !dirty;
    break;
  case 42:
    obj["wet"] = wet;
    obj["dirty"] = dirty;
    submit("diaper", obj);
    break;
  case 50:
    action("pump_start");
    break;
  // Stop commits immediately. The next screen annotates the completed row, versioned.
  case 51:
    action("pump_stop", "pump");
    break;
  case 52:
    pumpAmount = max(0, pumpAmount - 10);
    break;
  case 53:
    pumpAmount = min(2000, pumpAmount + 10);
    break;
  case 54: {
    DynamicJsonDocument e(2048);
    deserializeJson(e, lastResultRow);
    obj["total_ml"] = pumpAmount;
    submit("pump_amount", obj, e.as<JsonObject>());
    break;
  }
  case 62:
    action("sleep_dismiss", "sleep");
    break;
  case 60:
    action("sleep_start");
    break;
  case 61:
    action("sleep_stop", "sleep");
    break;
  case 90:
    nextPoll = 0;
    reviewing = false;
    break;
  case 91:
    retryAt = 0;
    failed = false;
    reviewing = false;
    break;
  case 92:
    if (rejected.length()) {
      rejected = "";
      rejectedReason = "";
      failed = false;
      reviewing = false;
      nextPoll = 0;
      tell("Refused log discarded");
    } else if (timerPending.length() && retryAt == UINT32_MAX) {
      prefs.remove("timer");
      timerPending = "";
      retryAt = 0;
      failed = false;
      reviewing = false;
      nextPoll = 0;
      tell("Timer rejection acknowledged");
    }
    break;
  }
  nextDrawMs = 0;
}
void touchRelease(int x, int y, uint32_t held) {
  lastTouch = millis();
  if (nightDimActive) {
    nightWakeUntil = millis() + 15000;
    return;
  }
  nightWakeUntil = millis() + 15000;
  if (curStatus == BS_CRYING && !cryAcked) {
    cryAcked = true;
    stopSong();
    buzz(25);
    return;
  }
  if (screen == DASHBOARD) {
    if (held >= 600) {
      page = page == PAGE_LAMP ? PAGE_STATUS : PAGE_LAMP;
      prefs.putString("boot", page == PAGE_LAMP ? "lamp" : "dashboard");
    } else if (BOX_MUTE.hit(x, y)) {
      volIdx = (volIdx + 1) % 4;
      prefs.putInt("vol", volIdx);
      if (isMuted())
        stopSong();
    } else
      navigate(HUB);
    nextDrawMs = 0;
    return;
  }
  if (failed && y >= 216) {
    reviewing = true;
    nextDrawMs = 0;
    return;
  }
  for (int i = 0; i < buttonCount; i++) {
    PadButton &b = buttons[i];
    if (b.enabled && Box{b.x, b.y, b.w, b.h}.hit(x, y)) {
      buzz(15);
      doAction(b.action);
      return;
    }
  }
}
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(BRIGHT_DAY);
  M5.Speaker.begin();
  M5.Power.setLed(0);
  prefs.begin("nurserypad", false);
  volIdx = prefs.getInt("vol", 2);
  caregiver = prefs.getString("caregiver", "");
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(320, 240);
  loadOutbox();
  String boot = prefs.getString("boot", "hub");
  if (boot != "hub") {
    screen = DASHBOARD;
    page = boot == "lamp" ? PAGE_LAMP : PAGE_STATUS;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  sntp_set_time_sync_notification_cb([](struct timeval *) { ntpSynced = true; });
  configTzTime(TZ_STRING, "pool.ntp.org", "time.nist.gov");
  commands = xQueueCreate(1, sizeof(NetCommand));
  results = xQueueCreate(2, sizeof(NetResult));
  xTaskCreatePinnedToCore(netTask, "nursery-net", 32768, nullptr, 1, nullptr, 0);
  lastTouch = millis();
}
void loop() {
  M5.update();
  serviceVibe();
  serviceSong();
  NetResult result;
  while (xQueueReceive(results, &result, 0) == pdTRUE)
    consume(result);
  if (!pending && WiFi.status() == WL_CONNECTED) {
    if ((int32_t)(millis() - nextPoll) >= 0) {
      request("device_snapshot", "{\"protocol\":1}");
      nextPoll =
          millis() + (screen == DASHBOARD && (curStatus == BS_SLEEPING || curStatus == BS_AWAY)
                          ? 30000
                          : 15000);
    } else if (retryAt != UINT32_MAX && (int32_t)(millis() - retryAt) >= 0 &&
               (timerPending.length() || queued)) {
      String envelope = timerPending.length() ? timerPending : outbox[0];
      request("device_op", "{\"envelope\":" + envelope + "}", true);
    } else if (statsWanted) {
      request("device_sleep_today", "{}");
      statsWanted = false;
    }
  }
  if (curStatus == BS_CRYING && sourceAge() < 180 && !sourceError.length() && !isMuted() &&
      !cryAcked && playingSong < 0 && page != PAGE_LAMP &&
      (int32_t)(millis() - cryNextLoopMs) >= 0) {
    playSong(SONG_STORMS);
    cryNextLoopMs = millis() + 5500;
  }
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    touchAt = millis();
    touchX = t.x;
    touchY = t.y;
  }
  if (t.wasReleased())
    touchRelease(touchX, touchY, millis() - touchAt);
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
    if (nightDimActive)
      nightWakeUntil = millis() + 15000;
    else
      navigate(HUB);
  }
  if (M5.BtnC.wasPressed()) {
    if (nightDimActive)
      nightWakeUntil = millis() + 15000;
    else {
      navigate(DASHBOARD);
      page = PAGE_STATUS;
    }
  }
  bool form =
      screen == BOTTLE || screen == DIAPER_SCREEN || screen == PUMP_AMOUNT || screen == FEED;
  if (screen != HUB && screen != DASHBOARD && millis() - lastTouch > (form ? 120000 : 45000))
    navigate(HUB);
  DisplayState ds = displayState();
  nightDimActive = screen == DASHBOARD && page != PAGE_LAMP && ds == DS_ASLEEP && inNightWindow() &&
                   (int32_t)(millis() - nightWakeUntil) >= 0 && sourceAge() < 900 &&
                   !sourceError.length();
  M5.Display.setBrightness(screen == DASHBOARD && page == PAGE_LAMP ? BRIGHT_LAMP
                           : nightDimActive                         ? BRIGHT_NIGHT
                           : ds == DS_CRYING                        ? BRIGHT_ALERT
                                                                    : BRIGHT_DAY);
  if ((int32_t)(millis() - nextDrawMs) >= 0) {
    if (screen != DASHBOARD)
      drawPad();
    else if (page == PAGE_LAMP)
      drawLampScreen();
    else if (nightDimActive)
      drawNightScreen();
    else
      drawStatusScreen();
    nextDrawMs = millis() + (ds == DS_CRYING ? 80 : 500);
  }
  delay(5);
}
