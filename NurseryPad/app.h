// NurseryPad application: state, transport, screens and input. Included once by the sketch.
// Kept in a header so the Arduino builder never generates prototypes for it: every function
// here is defined before it is used.
#pragma once
#include "cradlewatch_types.h"
#include "pad_types.h"
#include "secrets.h"
#include "supabase_certs.h"
#include <ArduinoJson.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <time.h>
#include "ui.h"

volatile bool ntpSynced = false;
time_t sourceObserved = 0, snapshotEpoch = 0;
uint32_t snapshotMs = 0;
String sourceError;
long sourceAge() {
  return sourceObserved && snapshotEpoch ? max(0L, (long)(snapshotEpoch - sourceObserved) +
                                                       (long)((millis() - snapshotMs) / 1000))
                                         : 1000000;
}
/** Now in epoch seconds: the synced clock, else the last snapshot's time plus local drift. */
time_t nowEpoch() {
  if (ntpSynced)
    return time(nullptr);
  return snapshotEpoch ? snapshotEpoch + (millis() - snapshotMs) / 1000 : 0;
}
#include "dashboard.h"
#include "m5go_leds.h"

// ---------------- state ----------------
Screen screen = HUB;
SleepTab sleepTab = TAB_LOG;
QueueHandle_t commands, results;
DynamicJsonDocument snapshot(16384), sleepStats(16384);
String outbox[8], timerPending, caregiver, lastResultRow;
// A one-shot log the server refused for good. Held for review; never blocks the queue.
String rejected, rejectedReason;
int queued = 0, amount = 60, pumpAmount = 0, retryStep = 0;
// After a timer op is applied, Switch/Stop stay disabled until a newer snapshot has arrived,
// so a second tap can never carry a stale expected_updated_at.
bool awaitSnapshot = false;
bool storageReady = true, bottleDraft = false, formula = false;
bool pending = false, failed = false, statsWanted = false;
String notice;                       // persistent problem line; tapping it opens Review
String toastText, undoOpId, undoRow; // transient confirmation and the one-shot log it can undo
uint32_t toastUntil = 0;
// Build with -DNURSERYPAD_PROFILE to print frame and input timing to serial at 115200. Off by
// default: it costs a timer read per frame and a serial write every five seconds.
#ifdef NURSERYPAD_PROFILE
uint32_t profFullUs = 0, profFullN = 0, profRectUs = 0, profRectN = 0;
uint32_t profSampleAt = 0, profWorstGap = 0, profReportAt = 0;
void profSampled() {
  uint32_t now = millis(), gap = now - profSampleAt;
  if (profSampleAt && gap > profWorstGap)
    profWorstGap = gap;
  profSampleAt = now;
  if ((int32_t)(now - profReportAt) < 0)
    return;
  profReportAt = now + 5000;
  Serial.printf("frame full %lu us x%lu | rect %lu us x%lu | worst input gap %lu ms\n",
                (unsigned long)(profFullN ? profFullUs / profFullN : 0), (unsigned long)profFullN,
                (unsigned long)(profRectN ? profRectUs / profRectN : 0), (unsigned long)profRectN,
                (unsigned long)profWorstGap);
  profFullUs = profFullN = profRectUs = profRectN = profWorstGap = 0;
}
#endif

bool dirty = true; // a full repaint is wanted before the next tick
uint32_t nextDraw = 0, lastTouch = 0, nextPoll = 0, retryAt = 0, touchAt = 0;
int pressX = 0, pressY = 0, moveX = 0, moveY = 0;
int armedId = 0; // the control this gesture began on; survives a roll off and back on
bool longFired = false, swallowTouch = false;

// Pressing a control changes one rectangle. Repainting and pushing all 320x240 for that costs
// about 31 ms of SPI time by itself, and M5.update() does not run while it happens, so the panel
// is blind to the next touch for the whole of it. Marking just the rectangle keeps both the
// repaint and the transfer proportional to what actually changed.
bool dirtyRect = false;
int dirtyX = 0, dirtyY = 0, dirtyW = 0, dirtyH = 0;
void markRect(int x, int y, int w, int h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > SCR_W)
    w = SCR_W - x;
  if (y + h > SCR_H)
    h = SCR_H - y;
  if (w <= 0 || h <= 0)
    return;
  if (!dirtyRect) {
    dirtyX = x;
    dirtyY = y;
    dirtyW = w;
    dirtyH = h;
    dirtyRect = true;
    return;
  }
  int right = max(dirtyX + dirtyW, x + w), foot = max(dirtyY + dirtyH, y + h);
  dirtyX = min(dirtyX, x);
  dirtyY = min(dirtyY, y);
  dirtyW = right - dirtyX;
  dirtyH = foot - dirtyY;
}
/** Marks a registered control, with room for its border and pressed highlight. */
void markControl(int id) {
  int x, y, w, h;
  if (id <= 0)
    return;
  if (!hitRect(id, x, y, w, h)) {
    dirty = true; // drawn before the current hit table existed: repaint everything
    return;
  }
  markRect(x - 3, y - 3, w + 6, h + 6);
}
/** Moves the highlight, repainting only the control losing it and the one taking it. */
void setPressed(int id) {
  if (id == pressedId)
    return;
  markControl(pressedId);
  pressedId = id;
  markControl(pressedId);
}
/** How far a finger may roll off a control before the press is abandoned. The old test allowed
 *  12 px at release only, which silently ate an ordinary thumb roll. */
const int CANCEL_SLOP = 22;
bool stillOn(int id, int x, int y) {
  int rx, ry, rw, rh;
  if (id <= 0)
    return false;
  if (!hitRect(id, rx, ry, rw, rh))
    return false; // it has left the screen since the press: the crying overlay, or a new snapshot
  return x >= rx - CANCEL_SLOP && x < rx + rw + CANCEL_SLOP && y >= ry - CANCEL_SLOP &&
         y < ry + rh + CANCEL_SLOP;
}
uint8_t brightness = 0;

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
/** Timer buttons need a fresh snapshot and nothing in flight. */
bool timersEnabled() { return live() && !timerPending.length() && !awaitSnapshot; }
/** One-shot logs only need somewhere to keep them. */
bool logsEnabled() { return storageReady && queued < 8; }

// ---------------- messages ----------------
void toast(const String &text, bool undoable = false) {
  toastText = text;
  toastUntil = millis() + 5000;
  if (!undoable) {
    undoOpId = "";
    undoRow = "";
  }
  dirty = true;
}
/** A problem the caregiver should see until it is resolved. */
void tell(const String &text) {
  notice = text;
  failed = true;
  dirty = true;
}
void clearNotice() {
  notice = "";
  failed = false;
  dirty = true;
}

// ---------------- storage ----------------
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
    tell("Stored queue needs recovery");
    return;
  }
  for (JsonVariant v : doc.as<JsonArray>()) {
    if (queued < 8 && v.is<const char *>() && v.as<String>().length() < 1536)
      outbox[queued++] = v.as<String>();
    else {
      storageReady = false;
      tell("Stored queue needs recovery");
      return;
    }
  }
  timerPending = prefs.getString("timer", "");
}

// ---------------- navigation ----------------
Screen backOf(Screen s) { return s == BOTTLE ? FEED : HUB; }
void navigate(Screen next) {
  screen = next;
  lastTouch = millis();
  nightWakeUntil = millis() + 15000;
  dirty = true;
  if (next == SLEEP)
    statsWanted = true;
}

// ---------------- transport (core 0) ----------------
void request(const char *rpc, const String &body, bool op = false) {
  if (body.length() >= 1536) {
    tell("Request too large");
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

// ---------------- writes ----------------
/** Queues an envelope. One-shot logs (bottle, diaper) persist locally and flush in order;
 *  timer ops need the network and go one at a time. Returns the client_op_id, or "" with a
 *  notice when nothing was queued. */
String submit(const char *op, JsonObject args, JsonObject target = JsonObject()) {
  if (!storageReady) {
    tell("Storage needs recovery; not saved");
    return "";
  }
  bool oneshot = String(op) == "bottle" || String(op) == "diaper";
  if (!oneshot && !ntpSynced) {
    tell("Waiting for clock sync");
    return "";
  }
  if (caregiver.isEmpty()) {
    tell("Select a caregiver first");
    return "";
  }
  if (!oneshot && (!live() || timerPending.length() || awaitSnapshot)) {
    tell("Wait for sync before timers");
    return "";
  }
  if (oneshot && queued == 8) {
    tell("Queue full (8). Connect to sync");
    return "";
  }
  DynamicJsonDocument doc(1536);
  String id = uuid();
  doc["protocol"] = 1;
  doc["client_op_id"] = id;
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
      tell("Storage failed. Not saved");
      return "";
    }
  } else {
    if (prefs.putString("timer", body) != body.length()) {
      tell("Storage failed. Not sent");
      return "";
    }
    timerPending = body;
  }
  retryAt = 0;
  dirty = true;
  return id;
}
String action(const char *op, const char *targetName = nullptr) {
  DynamicJsonDocument args(256);
  JsonObject a = args.to<JsonObject>();
  if (String(op) == "sleep_start")
    a["place"] = "crib";
  return submit(op, a, targetName ? snapshot[targetName].as<JsonObject>() : JsonObject());
}
long elapsed(JsonObject timer) {
  if (timer.isNull())
    return 0;
  if (timer.containsKey("elapsed_s"))
    return timer["elapsed_s"].as<long>() + (millis() - snapshotMs) / 1000;
  return max(0L, (long)(snapshotEpoch - parseIso8601Utc(timer["open_since"] | "")) +
                     (long)((millis() - snapshotMs) / 1000));
}
/** Seconds on one breast: the snapshot's total plus local drift while it is the active side. */
long sideSecs(const char *side) {
  JsonObject bf = snapshot["bf"].as<JsonObject>();
  if (bf.isNull())
    return 0;
  long s = bf[String(side) + "_s"] | 0L;
  if (String(bf["side"] | "") == side)
    s += (millis() - snapshotMs) / 1000;
  return s;
}
void dropHead() {
  for (int i = 1; i < queued; i++)
    outbox[i - 1] = outbox[i];
  if (queued)
    outbox[--queued] = "";
  if (!persistOutbox())
    tell("Saved; queue storage needs repair");
}
/** The server refused the oldest one-shot log for good: park it for review so the logs behind
 *  it still flush, and say why. */
void rejectHead(const String &reason) {
  if (!queued)
    return;
  rejected = outbox[0];
  rejectedReason = reason;
  dropHead();
  tell("Log refused. Tap to review");
}
/** Undo the last one-shot log while its toast shows: pull it from the queue if it has not
 *  been sent, otherwise delete the row the server created. */
void undoLast() {
  if (undoOpId.isEmpty())
    return;
  for (int i = 0; i < queued; i++) {
    DynamicJsonDocument e(1536);
    if (deserializeJson(e, outbox[i]) == DeserializationError::Ok && String(e["client_op_id"] | "") == undoOpId) {
      for (int j = i + 1; j < queued; j++)
        outbox[j - 1] = outbox[j];
      outbox[--queued] = "";
      persistOutbox();
      undoOpId = "";
      toast("Undone");
      return;
    }
  }
  if (undoRow.length()) {
    DynamicJsonDocument e(2048);
    deserializeJson(e, undoRow);
    DynamicJsonDocument args(64);
    if (submit("delete", args.to<JsonObject>(), e.as<JsonObject>()).length())
      toast("Undoing...");
    undoOpId = "";
    undoRow = "";
    return;
  }
  toast("Sending; undo it on a phone");
  undoOpId = "";
}

// ---------------- responses (UI core) ----------------
void consume(NetResult &r) {
  pending = false;
  if (r.code != 200 || !r.body) {
    // 401/403 on any call means the pairing is gone. Another 4xx on an op is the server's
    // final word on that envelope; everything else (5xx, transport, oversize) is retried
    // with backoff, never left wedged.
    if (r.code == 401 || r.code == 403) {
      tell("Device rejected - check pairing");
      retryAt = millis() + backoffMs();
    } else if (r.operation && r.code >= 400 && r.code < 500) {
      if (timerPending.length()) {
        tell("Timer refused. Tap to review");
        retryAt = UINT32_MAX;
      } else {
        rejectHead("http_" + String(r.code));
        retryAt = 0;
      }
    } else {
      tell("Connection failed - retrying");
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
    tell("Invalid response - retrying");
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
      } else {
        // Keep the created row while its toast still offers Undo.
        if (String(sent["client_op_id"] | "") == undoOpId && !doc["row"].isNull()) {
          undoRow = "";
          serializeJson(doc["row"], undoRow);
        }
        dropHead();
      }
      if (op == "bf_stop") {
        lastResultRow = "";
        serializeJson(doc["row"], lastResultRow);
        navigate(FEED_DONE);
      } else if (op == "pump_stop") {
        lastResultRow = "";
        serializeJson(doc["row"], lastResultRow);
        pumpAmount = 0;
        navigate(PUMP_AMOUNT);
      } else if (op == "pump_amount" || op == "delete") {
        if (op == "delete" && undoRow.isEmpty() && screen == HUB)
          toast("Undone");
        else
          navigate(HUB);
      } else if (op == "bf_start" || op == "bf_switch")
        navigate(FEED_TIMER);
      else if (op == "pump_start")
        navigate(PUMP_TIMER);
      if (op == "sleep_start" || op == "sleep_stop" || op == "sleep_dismiss")
        statsWanted = true;
      nextPoll = 0;
    } else if (outcome == "retry" || outcome.isEmpty()) {
      tell("Server busy - retrying");
      retryAt = millis() + backoffMs();
    } else if (timerPending.length()) {
      // A timer rejection is retained until explicitly acknowledged; nothing else is queued
      // behind it because timers are never submitted while one is pending.
      tell(outcome == "conflict" ? "Changed elsewhere. Tap to refresh" : "Timer refused. Tap to review");
      retryAt = UINT32_MAX;
    } else {
      rejectHead(outcome + (doc["reason"].is<const char *>() ? ": " + String(doc["reason"].as<const char *>()) : ""));
      retryAt = 0;
    }
    dirty = true;
    return;
  }
  if (r.stats) {
    sleepStats.set(doc);
    dirty = true;
    return;
  }
  if (!doc["protocol"].is<int>() || doc["protocol"].as<int>() != 1 ||
      !doc["snapshot_at"].is<const char *>() || !doc["caregivers"].is<JsonArray>()) {
    tell("Update firmware: protocol mismatch");
    return;
  }
  for (const char *name : {"bf", "pump", "sleep"})
    if (!doc[name].isNull() &&
        (!doc[name].is<JsonObject>() || !doc[name]["id"].is<const char *>() ||
         !doc[name]["updated_at"].is<const char *>() ||
         !doc[name]["open_since"].is<const char *>())) {
      tell("Invalid timer data");
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
  metrics.bed = two(metrics.bedMin / 60) + ":" + two(metrics.bedMin % 60);
  metrics.rise = two(metrics.riseMin / 60) + ":" + two(metrics.riseMin % 60);
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
  awaitSnapshot = false;
  // A rejected operation keeps its warning until the caregiver reviews it.
  if (retryAt != UINT32_MAX && !rejected.length() && storageReady) {
    failed = false;
    notice = "";
  }
  // A timer screen whose timer ended elsewhere falls back to the hub.
  if ((screen == FEED_TIMER && doc["bf"].isNull()) || (screen == PUMP_TIMER && doc["pump"].isNull()))
    if (!timerPending.length())
      navigate(HUB);
  dirty = true;
}

// ---------------- derived text ----------------
String caregiverName() {
  for (JsonObject c : snapshot["caregivers"].as<JsonArray>())
    if (c["id"].as<String>() == caregiver)
      return c["name"].as<String>();
  return "Caregiver";
}
String childName() { return snapshot["child_name"] | "Nursery"; }
/** "2h 10m ago" for an ISO timestamp, "" when absent. */
String agoOf(JsonVariant at) {
  time_t t = parseIso8601Utc(at | "");
  if (!t || !nowEpoch())
    return "";
  return fmtAgo((long)(nowEpoch() - t));
}
String feedSummary(bool tileStyle) {
  JsonObject bf = snapshot["bf"].as<JsonObject>();
  if (!bf.isNull())
    return "feeding  ·  " + fmtTimer(elapsed(bf));
  JsonObject last = snapshot["last_feed"].as<JsonObject>();
  if (last.isNull())
    return tileStyle ? "no feeds yet" : "";
  String kind = last["kind"] | "";
  String what = kind == "breastfeed" ? "breast" : String((int)(last["ml"] | 0.0)) + " ml bottle";
  return tileStyle ? what + "  ·  " + agoOf(last["at"]) : "fed " + what + " " + agoOf(last["at"]);
}
String diaperSummary(bool tileStyle) {
  JsonObject last = snapshot["last_diaper"].as<JsonObject>();
  if (last.isNull())
    return tileStyle ? "no changes yet" : "";
  String kind = last["kind"] | "wet";
  return tileStyle ? kind + "  ·  " + agoOf(last["at"]) : kind + " " + agoOf(last["at"]);
}
String pumpSummary() {
  JsonObject p = snapshot["pump"].as<JsonObject>();
  if (!p.isNull())
    return "pumping  ·  " + fmtTimer(elapsed(p));
  JsonObject last = snapshot["last_pump"].as<JsonObject>();
  if (last.isNull())
    return "no pumps yet";
  return String((int)(last["ml"] | 0.0)) + " ml  ·  " + agoOf(last["at"]);
}
String cribSummary() {
  JsonObject sl = snapshot["sleep"].as<JsonObject>();
  if (!sl.isNull() && String(sl["source"] | "") != "cradlewise")
    return "nap  ·  " + fmtTimer(elapsed(sl));
  DisplayState ds = displayState();
  long secs = sinceSecs();
  String dur = secs >= 0 ? "  " + fmtDur(secs) : "";
  switch (ds) {
  case DS_ASLEEP:
    return "asleep" + dur;
  case DS_SETTLING:
    return "settling" + dur;
  case DS_STIRRING:
    return "stirring";
  case DS_AWAKE:
    return "awake" + dur;
  case DS_CRYING:
    return "crying";
  case DS_AWAY:
    return "out of crib";
  default:
    return "no crib data";
  }
}
String sleepSummary() {
  JsonObject sl = snapshot["sleep"].as<JsonObject>();
  if (!sl.isNull() && String(sl["source"] | "") == "cradlewise")
    return "crib nap  ·  " + fmtTimer(elapsed(sl));
  return cribSummary();
}
/** The timer the strip should show: any running timer whose own screen is not open. */
const char *stripTimer() {
  if (!snapshot["bf"].isNull() && screen != FEED_TIMER && screen != FEED_DONE)
    return "bf";
  if (!snapshot["pump"].isNull() && screen != PUMP_TIMER && screen != PUMP_AMOUNT)
    return "pump";
  if (!snapshot["sleep"].isNull() && screen != SLEEP)
    return "sleep";
  return nullptr;
}
const char *titleOf(Screen s) {
  switch (s) {
  case FEED:
    return "Feed";
  case FEED_TIMER:
  case FEED_DONE:
    return "Breastfeed";
  case BOTTLE:
    return "Bottle";
  case CHANGE_DIAPER:
    return "Change";
  case PUMP:
  case PUMP_TIMER:
  case PUMP_AMOUNT:
    return "Pump";
  case SLEEP:
    return "Sleep";
  case REVIEW:
    return "Review";
  default:
    return "";
  }
}

// ---------------- chrome ----------------
void drawTopBar() {
  hitBand(0, BAR_H); // nothing in the bar may grow past it, and nothing outside may grow into it
  const int cy = BAR_H / 2;
  canvas.drawFastHLine(0, BAR_H - 1, SCR_W, C_LINE);
  int x = 12;
  if (screen != HUB) {
    if (hitDown(A_BACK))
      canvas.fillRoundRect(0, 0, 44, BAR_H - 1, R_BTN, C_PANEL_HI);
    iconChevronLeft(20, cy, C_TEXT);
    hit(0, 0, 44, BAR_H, A_BACK);
    x = 46;
  }
  // crib circle: the way into Dashboard mode from anywhere
  DisplayState ds = displayState();
  uint16_t ink;
  uint16_t fill = stateColor(ds, ink);
  bool hollow = ds == DS_BOOT;
  if (ds == DS_CRYING && (millis() / 400) % 2)
    fill = lerpCol(C_CRYING, C_BG, 0.6f);
  if (hitDown(A_DASH))
    canvas.fillRoundRect(276, 0, 44, BAR_H - 1, R_BTN, C_PANEL_HI);
  cribCircle(298, cy, hollow ? C_FAINT : fill, C_BORDER, hitDown(A_DASH) ? C_PANEL_HI : C_BG, hollow);
  hit(276, 0, 44, BAR_H, A_DASH);
  // caregiver chip
  String who = fit(caregiverName(), F_SMALL, 90);
  int chipW = textW(who, F_SMALL) + 34, chipX = 276 - 4 - chipW;
  uint16_t chipFill = hitDown(A_CAREGIVER) ? C_PANEL_HI : C_PANEL;
  panel(chipX, cy - 14, chipW, 28, chipFill, C_BORDER, 14);
  hit(chipX, 0, chipW, BAR_H, A_CAREGIVER);
  text(who, chipX + 12, cy, F_SMALL, C_TEXT, chipFill, lgfx::textdatum_t::middle_left);
  iconChevronDown(chipX + chipW - 14, cy, C_MUTED);
  // clock and the queued-writes dot
  String clk = ntpSynced ? clockStr(time(nullptr)) : "--:--";
  int clkW = textW(clk, F_SMALL);
  text(clk, chipX - 8, cy, F_SMALL, C_MUTED, C_BG, lgfx::textdatum_t::middle_right);
  int right = chipX - 8 - clkW - 8;
  if (queued || timerPending.length()) {
    canvas.fillCircle(right - 4, cy, 4, failed ? C_AMBER : C_RED);
    right -= 14;
  }
  String title = screen == HUB ? childName() : titleOf(screen);
  text(fit(title, F_BODY, right - x), x, cy, F_BODY, C_TEXT, C_BG, lgfx::textdatum_t::middle_left);
  hitBand(0, SCR_H);
}
void drawStrip() {
  const char *timer = stripTimer();
  if (!timer)
    return;
  int y = SCR_H - STRIP_H;
  hitBand(y, SCR_H);
  canvas.fillRect(0, y, SCR_W, STRIP_H, C_PANEL);
  canvas.drawFastHLine(0, y, SCR_W, C_BORDER);
  bool bf = String(timer) == "bf", pump = String(timer) == "pump";
  uint16_t accent = bf ? C_AMBER : pump ? C_PURPLE : C_BLUE;
  String label = bf     ? "BREASTFEED  ·  " + String(snapshot["bf"]["side"] | "")
                 : pump ? "PUMP"
                 : String(snapshot["sleep"]["source"] | "") == "cradlewise" ? "CRIB NAP"
                                                                          : "NAP";
  label.toUpperCase();
  if (hitDown(A_STRIP_OPEN))
    canvas.fillRect(0, y + 1, 250, STRIP_H - 1, C_PANEL_HI);
  canvas.fillRoundRect(10, y + 8, 4, 20, 2, accent);
  text(label, 22, y + 18, F_SMALL, C_TEXT, C_PANEL, lgfx::textdatum_t::middle_left);
  int lx = 22 + textW(label, F_SMALL) + 10;
  text(fmtTimer(elapsed(snapshot[timer].as<JsonObject>())), lx, y + 18, F_BODY, C_TEXT, C_PANEL,
       lgfx::textdatum_t::middle_left);
  hit(0, y, 250, STRIP_H, A_STRIP_OPEN);
  bool enabled = timersEnabled();
  if (hitDown(A_STRIP_STOP))
    canvas.fillRect(250, y + 1, 70, STRIP_H - 1, C_PANEL_HI);
  canvas.drawFastVLine(250, y, STRIP_H, C_BORDER);
  text("STOP", 285, y + 18, F_SMALL, enabled ? C_TEXT : C_FAINT, C_PANEL);
  hit(250, y, 70, STRIP_H, A_STRIP_STOP, enabled);
  hitBand(0, SCR_H);
}
void drawNotice(int y) {
  canvas.fillRect(0, y, SCR_W, 22, C_BG);
  canvas.drawFastHLine(0, y, SCR_W, C_LINE);
  text(fit(notice, F_SMALL, SCR_W - 16), 160, y + 11, F_SMALL, C_AMBER, C_BG);
  hit(0, y - 6, SCR_W, 28, A_NOTICE);
}
void drawToast(bool strip) {
  if (!toastText.length() || (int32_t)(toastUntil - millis()) <= 0)
    return;
  int y = (strip ? SCR_H - STRIP_H : SCR_H) - PAD - 36;
  panel(PAD, y, SCR_W - 2 * PAD, 36, C_TOAST, 0, R_TILE);
  bool undo = undoOpId.length();
  text(fit(toastText, F_SMALL, SCR_W - 2 * PAD - (undo ? 90 : 24)), PAD + 12, y + 18, F_SMALL, C_TOAST_INK, C_TOAST,
       lgfx::textdatum_t::middle_left);
  if (undo) {
    if (hitDown(A_UNDO))
      canvas.fillRoundRect(SCR_W - PAD - 68, y + 4, 62, 28, 7, lerpCol(C_TOAST, C_UNDO, 0.15f));
    text("UNDO", SCR_W - PAD - 37, y + 18, F_SMALL, C_UNDO, C_TOAST);
    hit(SCR_W - PAD - 74, y, 74, 36, A_UNDO);
  }
}
void drawCryingOverlay() {
  hitCount = 0;
  canvas.fillScreen(C_CRYING);
  int r = 44 + (int)((millis() % 1100) * 100 / 1100);
  uint16_t ring = lerpCol(C_CRYING, C_WHITE, 0.85f);
  for (int o = 0; o < 4; ++o)
    canvas.drawCircle(160, 112, r + o, ring);
  text("Crying", 160, 104, F_HERO, C_WHITE, C_CRYING);
  long secs = sinceSecs();
  text(secs >= 0 ? "since " + clockStr(sinceEpoch) + "  ·  " + fmtDur(secs) : "", 160, 144, F_SMALL,
       lerpCol(C_CRYING, C_WHITE, 0.9f), C_CRYING);
  text("tap to silence the chime", 160, 220, F_SMALL, lerpCol(C_CRYING, C_WHITE, 0.8f), C_CRYING);
  hit(0, 0, SCR_W, SCR_H, A_SILENCE);
}

// ---------------- screens ----------------
void hubTile(int x, int y, int w, int h, int id, uint16_t accent, const char *label, const String &sub, int icon) {
  uint16_t fill = tile(x, y, w, h, id, C_PANEL);
  int iy = y + (h < 72 ? 14 : 18);
  switch (icon) {
  case 0:
    iconDrop(x + 22, iy, accent);
    break;
  case 1:
    iconSwap(x + 22, iy, accent);
    break;
  case 2:
    iconPump(x + 22, iy, accent);
    break;
  default:
    iconMoon(x + 22, iy, accent, fill);
  }
  text(label, x + 12, y + h - 30, F_BODY, accent, fill, lgfx::textdatum_t::middle_left);
  text(fit(sub, F_SMALL, w - 24), x + 12, y + h - 13, F_SMALL, C_MUTED, fill, lgfx::textdatum_t::middle_left);
}
void drawHub(int top, int bottom) {
  // status line: crib first in text colour, then the day's log in muted
  String crib = "Crib " + cribSummary();
  if (!snapshotEpoch)
    crib = WiFi.status() == WL_CONNECTED ? "Connecting to the log..." : "Joining Wi-Fi...";
  text(fit(crib, F_SMALL, SCR_W - 2 * PAD), PAD, top + 11, F_SMALL, C_TEXT, C_BG, lgfx::textdatum_t::middle_left);
  int cx = PAD + textW(crib, F_SMALL);
  String rest;
  String fed = feedSummary(false), wet = diaperSummary(false);
  if (fed.length())
    rest += "  ·  " + fed;
  if (wet.length())
    rest += "  ·  " + wet;
  if (rest.length() && cx < SCR_W - 60)
    text(fit(rest, F_SMALL, SCR_W - PAD - cx), cx, top + 11, F_SMALL, C_MUTED, C_BG, lgfx::textdatum_t::middle_left);
  int y0 = top + 24, avail = bottom - PAD - y0;
  int th = (avail - GAP) / 2, tw = (SCR_W - 2 * PAD - GAP) / 2;
  hubTile(PAD, y0, tw, th, A_FEED, C_AMBER, "FEED", feedSummary(true), 0);
  hubTile(PAD + tw + GAP, y0, tw, th, A_CHANGE, C_TEAL, "CHANGE", diaperSummary(true), 1);
  hubTile(PAD, y0 + th + GAP, tw, th, A_PUMP, C_PURPLE, "PUMP", pumpSummary(), 2);
  hubTile(PAD + tw + GAP, y0 + th + GAP, tw, th, A_SLEEP, C_BLUE, "SLEEP", sleepSummary(), 3);
}
void drawFeed(int top, int bottom) {
  bool enabled = timersEnabled();
  int y0 = top + GAP, bh = 48, tw = (SCR_W - 2 * PAD - GAP) / 2;
  int th = bottom - PAD - bh - GAP - y0;
  const char *names[2] = {"LEFT", "RIGHT"};
  int ids[2] = {A_LEFT, A_RIGHT};
  for (int i = 0; i < 2; i++) {
    int x = PAD + i * (tw + GAP);
    uint16_t fill = tile(x, y0, tw, th, ids[i], C_AMBER_BG, C_AMBER, 2, enabled);
    text(names[i], x + tw / 2, y0 + th / 2 - 9, F_BIG, enabled ? C_TEXT : C_FAINT, fill);
    text(enabled ? "start" : "waiting for sync", x + tw / 2, y0 + th / 2 + 16, F_SMALL, enabled ? C_AMBER : C_MUTED, fill);
  }
  int by = bottom - PAD - bh;
  uint16_t fill = tile(PAD, by, SCR_W - 2 * PAD, bh, A_BOTTLE, C_PANEL, C_BORDER, 1, logsEnabled());
  text("BOTTLE", PAD + 16, by + bh / 2, F_BODY, C_TEXT, fill, lgfx::textdatum_t::middle_left);
  text("last " + String(constrain((int)(snapshot["bottle_default_ml"] | 60.0), 1, 1000)) + " ml", SCR_W - PAD - 16,
       by + bh / 2, F_SMALL, C_MUTED, fill, lgfx::textdatum_t::middle_right);
}
void drawFeedTimer(int top, int bottom) {
  JsonObject bf = snapshot["bf"].as<JsonObject>();
  bool enabled = timersEnabled() && !bf.isNull();
  int bh = 52, by = bottom - PAD - bh, mid = top + (by - top) / 2;
  String side = bf["side"] | "";
  side.toUpperCase();
  text(side, 160, top + 16, F_SMALL, C_AMBER, C_BG);
  text(fmtTimer(elapsed(bf)), 160, mid, F_TIMER, C_TEXT, C_BG);
  text("L " + fmtTimer(sideSecs("left")) + "     R " + fmtTimer(sideSecs("right")), 160, mid + 40, F_SMALL, C_MUTED, C_BG);
  int w = (SCR_W - 2 * PAD - GAP) / 2;
  String other = String(bf["side"] | "") == "left" ? "R" : "L";
  quietButton(PAD, by, w, bh, A_SWITCH, "SWITCH  >  " + other, C_TEXT, F_BODY, enabled);
  primaryButton(PAD + w + GAP, by, w, bh, A_STOP_BF, "STOP", C_AMBER, C_AMBER_INK, F_BODY, enabled);
}
void drawFeedDone(int top, int bottom) {
  DynamicJsonDocument done(2048);
  deserializeJson(done, lastResultRow);
  long l = done["payload"]["left_s"] | 0, r = done["payload"]["right_s"] | 0;
  int bh = 48, by = bottom - PAD - bh, mid = top + (by - top) / 2;
  text("Breastfeed saved  ·  " + caregiverName(), 160, top + 16, F_SMALL, C_MUTED, C_BG);
  text(fmtTimer(l + r), 160, mid, F_HERO, C_TEXT, C_BG);
  text("L " + fmtTimer(l) + "     R " + fmtTimer(r), 160, mid + 32, F_SMALL, C_MUTED, C_BG);
  int w1 = (SCR_W - 2 * PAD - GAP) / 3, w2 = SCR_W - 2 * PAD - GAP - w1;
  quietButton(PAD, by, w1, bh, A_DELETE, "Delete", C_MUTED, F_SMALL, timersEnabled());
  primaryButton(PAD + w1 + GAP, by, w2, bh, A_DONE, "DONE", C_AMBER, C_AMBER_INK);
}
/** Minus / value / plus row centred between `top` and `bottom`. */
void stepper(int top, int bottom, int value, uint16_t accent) {
  int s = 60, cy = top + (bottom - top) / 2;
  if (cy - s / 2 < top) // a strip and a notice together leave too little room to centre in
    cy = top + s / 2;
  uint16_t fm = tile(PAD + 24, cy - s / 2, s, s, A_MINUS, C_PANEL);
  iconMinus(PAD + 24 + s / 2, cy, C_TEXT);
  (void)fm;
  uint16_t fp = tile(SCR_W - PAD - 24 - s, cy - s / 2, s, s, A_PLUS, C_PANEL);
  iconPlus(SCR_W - PAD - 24 - s / 2, cy, C_TEXT);
  (void)fp;
  text(String(value), 160, cy - 4, F_HERO, C_TEXT, C_BG);
  text("ml", 160, cy + 26, F_SMALL, accent, C_BG);
}
void drawBottle(int top, int bottom) {
  int bh = 48, sh = 36;
  int by = bottom - PAD - bh, sy = by - GAP - sh;
  stepper(top, sy, amount, C_MUTED);
  segmented(PAD, sy, SCR_W - 2 * PAD, sh, A_BREAST, A_FORMULA, "Breast milk", "Formula", !formula);
  primaryButton(PAD, by, SCR_W - 2 * PAD, bh, A_SAVE_BOTTLE, "SAVE", C_AMBER, C_AMBER_INK, F_BODY, logsEnabled());
}
void drawChange(int top, int bottom) {
  bool enabled = logsEnabled();
  int y0 = top + GAP, h = bottom - PAD - y0, w = (SCR_W - 2 * PAD - 2 * GAP) / 3;
  JsonObject last = snapshot["last_diaper"].as<JsonObject>();
  String lastKind = last["kind"] | "";
  String ago = agoOf(last["at"]);
  const char *names[3] = {"WET", "DIRTY", "BOTH"};
  const char *kinds[3] = {"wet", "dirty", "both"};
  int ids[3] = {A_WET, A_DIRTY, A_BOTH};
  for (int i = 0; i < 3; i++) {
    int x = PAD + i * (w + GAP);
    bool recent = lastKind == kinds[i];
    uint16_t fill = tile(x, y0, w, h, ids[i], recent ? C_TEAL_BG : C_PANEL, recent ? C_TEAL_BORDER : C_BORDER, 1, enabled);
    text(names[i], x + w / 2, y0 + h / 2 - 10, F_BIG, !enabled ? C_FAINT : recent ? C_TEAL : C_TEXT, fill);
    text(recent && ago.length() ? ago : "one tap saves", x + w / 2, y0 + h / 2 + 16, F_SMALL, C_MUTED, fill);
  }
}
void drawPump(int top, int bottom) {
  bool enabled = timersEnabled();
  int y0 = top + GAP, h = bottom - PAD - y0;
  uint16_t fill = tile(PAD, y0, SCR_W - 2 * PAD, h, A_START_PUMP, C_PURPLE_BG, C_PURPLE, 2, enabled);
  iconPlay(160, y0 + h / 2 - 34, enabled ? C_PURPLE : C_FAINT);
  text("START PUMP", 160, y0 + h / 2 + 2, F_BIG, enabled ? C_TEXT : C_FAINT, fill);
  JsonObject last = snapshot["last_pump"].as<JsonObject>();
  String sub = enabled ? (last.isNull() ? "no pumps yet" : "last " + String((int)(last["ml"] | 0.0)) + " ml  ·  " + agoOf(last["at"]))
                       : "waiting for sync";
  text(sub, 160, y0 + h / 2 + 30, F_SMALL, C_MUTED, fill);
}
void drawPumpTimer(int top, int bottom) {
  JsonObject p = snapshot["pump"].as<JsonObject>();
  bool enabled = timersEnabled() && !p.isNull();
  int bh = 52, by = bottom - PAD - bh, mid = top + (by - top) / 2;
  text("PUMPING", 160, top + 16, F_SMALL, C_PURPLE, C_BG);
  text(fmtTimer(elapsed(p)), 160, mid, F_TIMER, C_TEXT, C_BG);
  text("amount is asked at the end  ·  " + caregiverName(), 160, mid + 40, F_SMALL, C_MUTED, C_BG);
  primaryButton(PAD, by, SCR_W - 2 * PAD, bh, A_STOP_PUMP, "STOP", C_PURPLE, C_PURPLE_INK, F_BODY, enabled);
}
void drawPumpAmount(int top, int bottom) {
  DynamicJsonDocument done(2048);
  deserializeJson(done, lastResultRow);
  long secs = max(0L, (long)(parseIso8601Utc(done["ended_at"] | "") - parseIso8601Utc(done["started_at"] | "")));
  text("Pumped " + fmtDur(secs) + "  ·  total amount", 160, top + 14, F_SMALL, C_MUTED, C_BG);
  int bh = 48, by = bottom - PAD - bh;
  stepper(top + 24, by - GAP, pumpAmount, C_PURPLE);
  int w1 = (SCR_W - 2 * PAD - GAP) / 3, w2 = SCR_W - 2 * PAD - GAP - w1;
  quietButton(PAD, by, w1, bh, A_SKIP_AMOUNT, "Skip", C_MUTED, F_SMALL);
  primaryButton(PAD + w1 + GAP, by, w2, bh, A_SAVE_AMOUNT, "SAVE", C_PURPLE, C_PURPLE_INK, F_BODY, timersEnabled());
}
/** Minutes as "0m", "45m", "1h 6m" for the stats grid. */
String fmtMin(double minutes) { return minutes < 1 ? String("0m") : fmtDur((long)(minutes * 60)); }
/** The 3x2 sleep grid with its rise/bed footer, shared by the Sleep tab and the dashboard. */
void drawStatsGrid(int y, int bottom, uint16_t bg) {
  {
    struct Cell {
      String n, cap;
    } cells[6] = {{fmtMin(sleepStats["sleep_min"] | 0.0), "total sleep"},
                  {String(sleepStats["naps"] | 0), "naps"},
                  {fmtMin(sleepStats["longest_nap_min"] | 0.0), "longest nap"},
                  {fmtMin(sleepStats["last_night_min"] | 0.0), "night sleep"},
                  {String(sleepStats["last_night_wakings"] | 0), "wakings"},
                  {sleepStats["awake_in_bed_s"].isNull() ? String("--") : fmtMin((sleepStats["awake_in_bed_s"] | 0.0) / 60),
                   "awake in bed"}};
    int fh = 18, gh = bottom - PAD - fh - y, ch = (gh - GAP) / 2, cw = (SCR_W - 2 * PAD - 2 * GAP) / 3;
    for (int i = 0; i < 6; i++) {
      int x = PAD + (i % 3) * (cw + GAP), yy = y + (i / 3) * (ch + GAP);
      panel(x, yy, cw, ch, C_PANEL, C_BORDER);
      text(cells[i].n, x + 10, yy + ch / 2 - 9, F_BIG, i == 0 ? C_BLUE : C_TEXT, C_PANEL,
           lgfx::textdatum_t::middle_left);
      text(cells[i].cap, x + 10, yy + ch / 2 + 14, F_SMALL, C_MUTED, C_PANEL, lgfx::textdatum_t::middle_left);
    }
    String foot = "rise " + metrics.rise + "  ·  bed " + metrics.bed;
    if (sourceObserved)
      foot += "  ·  crib data " + fmtAgo(sourceAge());
    text(fit(foot, F_SMALL, SCR_W - 2 * PAD), PAD, bottom - PAD - 8, F_SMALL, C_MUTED, bg, lgfx::textdatum_t::middle_left);
  }
}
/** Local "13:04" for an ISO timestamp; "now" when absent. */
String hm(JsonVariant at) {
  time_t t = parseIso8601Utc(at | "");
  return t ? clockStr(t) : String("now");
}
void drawSleep(int top, int bottom) {
  // tabs
  int ty = top, th = 32, half = SCR_W / 2;
  canvas.drawFastHLine(0, ty + th - 1, SCR_W, C_LINE);
  for (int i = 0; i < 2; i++) {
    bool on = (i == 0) == (sleepTab == TAB_LOG);
    int id = i == 0 ? A_TAB_LOG : A_TAB_STATS;
    if (hitDown(id))
      canvas.fillRect(i * half, ty, half, th - 1, C_PANEL);
    text(i == 0 ? "LOG" : "STATS", i * half + half / 2, ty + th / 2, F_SMALL, on ? C_TEXT : C_MUTED, C_BG);
    if (on)
      canvas.fillRect(i * half + 24, ty + th - 3, half - 48, 3, C_BLUE);
    hit(i * half, ty, half, th, id);
  }
  int y = ty + th + GAP;
  bool enabled = timersEnabled();
  JsonObject sl = snapshot["sleep"].as<JsonObject>();
  bool running = !sl.isNull(), automatic = running && String(sl["source"] | "") == "cradlewise";
  if (sleepTab == TAB_LOG) {
    if (automatic) {
      int h = 70;
      panel(PAD, y, SCR_W - 2 * PAD, h, C_GREEN_BG, C_GREEN_BORDER);
      if ((millis() / 800) % 2)
        canvas.fillCircle(PAD + 16, y + 17, 5, C_GREEN_DOT);
      else
        canvas.drawCircle(PAD + 16, y + 17, 5, C_GREEN_DOT);
      text("Crib nap", PAD + 30, y + 17, F_BODY, C_TEXT, C_GREEN_BG, lgfx::textdatum_t::middle_left);
      text("auto  ·  since " + hm(sl["open_since"]), PAD + 30 + textW("Crib nap", F_BODY) + 10, y + 18, F_SMALL, C_MUTED,
           C_GREEN_BG, lgfx::textdatum_t::middle_left);
      text(fmtTimer(elapsed(sl)), SCR_W - PAD - 12, y + 17, F_BODY, C_TEXT, C_GREEN_BG, lgfx::textdatum_t::middle_right);
      int bw = (SCR_W - 2 * PAD - 24 - GAP) / 2;
      quietButton(PAD + 12, y + 34, bw, 28, A_END_AUTO, "End now", C_TEXT, F_SMALL, enabled);
      quietButton(PAD + 12 + bw + GAP, y + 34, bw, 28, A_DISMISS_AUTO, "Not a nap", C_MUTED, F_SMALL, enabled);
      y += h + GAP;
    } else if (running) {
      primaryButton(PAD, y, SCR_W - 2 * PAD, 48, A_STOP_NAP, "", C_BLUE, C_BLUE_INK, F_BODY, enabled);
      uint16_t f = enabled ? (hitDown(A_STOP_NAP) ? lerpCol(C_BLUE, C_WHITE, 0.18f) : C_BLUE) : C_PANEL;
      text("STOP NAP", PAD + 16, y + 24, F_BODY, enabled ? C_BLUE_INK : C_FAINT, f, lgfx::textdatum_t::middle_left);
      text(fmtTimer(elapsed(sl)), SCR_W - PAD - 16, y + 24, F_BODY, enabled ? C_BLUE_INK : C_FAINT, f,
           lgfx::textdatum_t::middle_right);
      y += 48 + GAP;
    } else {
      uint16_t fill = tile(PAD, y, SCR_W - 2 * PAD, 48, A_START_NAP, C_BLUE_BG, C_BLUE, 2, enabled);
      iconPlay(PAD + 22, y + 24, enabled ? C_BLUE : C_FAINT);
      text("START NAP", PAD + 40, y + 24, F_BODY, enabled ? C_TEXT : C_FAINT, fill, lgfx::textdatum_t::middle_left);
      text(enabled ? "outside the crib" : "waiting for sync", SCR_W - PAD - 16, y + 24, F_SMALL, C_MUTED, fill,
           lgfx::textdatum_t::middle_right);
      y += 48 + GAP;
    }
    // today's sleeps, newest first
    int shown = 0;
    for (JsonObject e : sleepStats["rows"].as<JsonArray>()) {
      if (y + 20 > bottom - 4)
        break;
      String when = hm(e["start"]) + " - " + hm(e["end"]);
      String where = String(e["place"] | "") + (String(e["source"] | "") == "cradlewise" ? "  ·  auto" : "");
      text(when, PAD + 6, y + 10, F_SMALL, C_TEXT, C_BG, lgfx::textdatum_t::middle_left);
      text("·  " + where, PAD + 6 + textW(when, F_SMALL) + 8, y + 10, F_SMALL, C_MUTED, C_BG, lgfx::textdatum_t::middle_left);
      text(fmtDur(e["duration_s"] | 0L), SCR_W - PAD - 6, y + 10, F_SMALL, C_TEXT, C_BG, lgfx::textdatum_t::middle_right);
      y += 20;
      shown++;
    }
    if (!shown)
      text(sleepStats.isNull() || sleepStats.size() == 0 ? "loading today..." : "no sleep logged today", 160, y + 12, F_SMALL,
           C_MUTED, C_BG);
  } else
    drawStatsGrid(y, bottom, C_BG);
}
/** Dashboard page two: today's sleep numbers on the dark ground, same chrome as the status page. */
void drawDashStats() {
  canvas.fillScreen(C_BG);
  hit(0, 0, SCR_W, SCR_H, A_DASH_TAP);
  iconSpeaker(22, 18, C_MUTED, volIdx);
  hit(0, 0, 44, BAR_H, A_VOL);
  String banner = bannerText();
  text(fit(banner.length() ? banner : "sleep today  ·  swipe for lamp", F_SMALL, 200), 160, 18, F_SMALL,
       banner.length() ? C_AMBER : C_MUTED, C_BG);
  canvas.fillCircle(298, 18, 13, C_MUTED);
  canvas.fillCircle(298, 18, 11, C_BG);
  iconHome(298, 18, C_MUTED);
  hit(276, 0, 44, BAR_H, A_DASH_HOME);
  drawStatsGrid(BAR_H + 4, SCR_H - 12, C_BG);
  drawPageDots(C_TEXT, C_FAINT);
}
void drawReview(int top, int bottom) {
  bool refused = rejected.length();
  String what = refused ? "A log was refused: " + rejectedReason : "A timer operation needs review";
  String why = refused ? "It was not saved. Log it again if needed." : "Refresh to see the latest log.";
  if (!refused && !timerPending.length())
    what = notice.length() ? notice : "Nothing to review";
  text(fit(what, F_SMALL, SCR_W - 2 * PAD), 160, top + 18, F_SMALL, C_AMBER, C_BG);
  text(fit(why, F_SMALL, SCR_W - 2 * PAD), 160, top + 40, F_SMALL, C_MUTED, C_BG);
  int bh = 44, w = (SCR_W - 2 * PAD - GAP) / 2, y1 = top + 60, y2 = bottom - PAD - bh;
  quietButton(PAD, y1, w, bh, A_REFRESH, "Refresh", C_TEXT, F_SMALL);
  quietButton(PAD + w + GAP, y1, w, bh, A_RETRY, "Retry", C_TEXT, F_SMALL, !refused && timerPending.length());
  if (refused || (timerPending.length() && retryAt == UINT32_MAX))
    quietButton(PAD, y2, SCR_W - 2 * PAD, bh, A_DISCARD, refused ? "Discard refused log" : "Drop the timer operation", C_AMBER,
                F_SMALL);
}
void drawPad() {
  canvas.fillScreen(C_BG);
  bool strip = stripTimer() != nullptr;
  int top = BAR_H, bottom = strip ? SCR_H - STRIP_H : SCR_H;
  if (notice.length() && screen != REVIEW)
    bottom -= 22;
  drawTopBar();
  hitBand(top, bottom);
  switch (screen) {
  case HUB:
    drawHub(top, bottom);
    break;
  case FEED:
    drawFeed(top, bottom);
    break;
  case FEED_TIMER:
    drawFeedTimer(top, bottom);
    break;
  case FEED_DONE:
    drawFeedDone(top, bottom);
    break;
  case BOTTLE:
    drawBottle(top, bottom);
    break;
  case CHANGE_DIAPER:
    drawChange(top, bottom);
    break;
  case PUMP:
    drawPump(top, bottom);
    break;
  case PUMP_TIMER:
    drawPumpTimer(top, bottom);
    break;
  case PUMP_AMOUNT:
    drawPumpAmount(top, bottom);
    break;
  case SLEEP:
    drawSleep(top, bottom);
    break;
  case REVIEW:
    drawReview(top, bottom);
    break;
  default:
    break;
  }
  hitBand(0, SCR_H);
  if (notice.length() && screen != REVIEW)
    drawNotice(bottom);
  if (strip)
    drawStrip();
  drawToast(strip);
}
/** Composes the whole frame into the sprite. Under a clip rectangle it still walks every widget,
 *  but writes pixels only inside it, which is where a sprite in PSRAM costs its time. */
void drawFrame() {
  hitCount = 0;
  hitBand(0, SCR_H);
  if (screen == DASHBOARD) {
    dashFooter = "";
    String fed = feedSummary(false), wet = diaperSummary(false);
    if (fed.length())
      dashFooter += fed;
    if (wet.length())
      dashFooter += String(dashFooter.length() ? "  ·  " : "") + wet;
    int naps = snapshot["today"]["naps"] | 0;
    dashFooter += String(dashFooter.length() ? "  ·  " : "") + "naps today " + String(naps);
    if (page == PAGE_LAMP)
      drawLampScreen();
    else if (page == PAGE_STATS)
      drawDashStats();
    else if (nightDimActive)
      drawNightScreen();
    else
      drawStatusScreen();
  } else {
    drawPad();
    if (displayState() == DS_CRYING && !cryAcked)
      drawCryingOverlay();
  }
}
void render() {
  dirty = false;
  dirtyRect = false;
#ifdef NURSERYPAD_PROFILE
  uint32_t t0 = micros();
#endif
  drawFrame();
  M5.Display.startWrite();
  canvas.pushSprite(0, 0);
  M5.Display.endWrite();
#ifdef NURSERYPAD_PROFILE
  profFullUs += micros() - t0;
  profFullN++;
#endif
}
/** Repaints and transfers one rectangle. pushImage clips before it transfers, so only this
 *  rectangle crosses the bus: a pressed control costs about a millisecond instead of thirty. */
void renderRect(int x, int y, int w, int h) {
  dirtyRect = false;
#ifdef NURSERYPAD_PROFILE
  uint32_t t0 = micros();
#endif
  canvas.setClipRect(x, y, w, h);
  drawFrame();
  canvas.clearClipRect();
  M5.Display.setClipRect(x, y, w, h);
  M5.Display.startWrite();
  canvas.pushSprite(0, 0);
  M5.Display.endWrite();
  M5.Display.clearClipRect();
#ifdef NURSERYPAD_PROFILE
  profRectUs += micros() - t0;
  profRectN++;
#endif
}

// ---------------- actions ----------------
void setPage(Page next) {
  page = next;
  prefs.putString("boot", page == PAGE_LAMP ? "lamp" : "dashboard");
  if (page == PAGE_STATS)
    statsWanted = true;
  nightWakeUntil = millis() + 15000;
  dirty = true;
}
void toggleLamp() {
  setPage(page == PAGE_LAMP ? PAGE_STATUS : PAGE_LAMP);
  buzz(30);
}

void stopRunning(const char *timer) {
  if (String(timer) == "bf")
    action("bf_stop", "bf");
  else if (String(timer) == "pump")
    action("pump_stop", "pump");
  else
    action("sleep_stop", "sleep");
}
void logDiaper(bool wet, bool dirty) {
  DynamicJsonDocument args(128);
  JsonObject a = args.to<JsonObject>();
  a["wet"] = wet;
  a["dirty"] = dirty;
  String id = submit("diaper", a);
  if (id.isEmpty())
    return;
  undoOpId = id;
  undoRow = "";
  navigate(HUB);
  toast(String(wet && dirty ? "Wet + dirty" : wet ? "Wet" : "Dirty") + " diaper logged", true);
}
void doAction(int a) {
  DynamicJsonDocument args(256);
  JsonObject obj = args.to<JsonObject>();
  switch (a) {
  case A_BACK:
    navigate(backOf(screen));
    break;
  case A_DASH:
    navigate(DASHBOARD);
    setPage(PAGE_STATUS);
    break;
  case A_CAREGIVER: {
    JsonArray cs = snapshot["caregivers"];
    for (size_t i = 0; i < cs.size(); i++)
      if (cs[i]["id"].as<String>() == caregiver) {
        caregiver = cs[(i + 1) % cs.size()]["id"].as<String>();
        prefs.putString("caregiver", caregiver);
        break;
      }
    break;
  }
  case A_NOTICE:
    navigate(REVIEW);
    break;
  case A_STRIP_OPEN: {
    String timer = stripTimer() ? stripTimer() : "";
    navigate(timer == "bf" ? FEED_TIMER : timer == "pump" ? PUMP_TIMER : SLEEP);
    break;
  }
  case A_STRIP_STOP:
    if (stripTimer())
      stopRunning(stripTimer());
    break;
  case A_UNDO:
    undoLast();
    break;
  case A_SILENCE:
    cryAcked = true;
    stopSong();
    buzz(25);
    break;
  case A_FEED:
    navigate(snapshot["bf"].isNull() ? FEED : FEED_TIMER);
    break;
  case A_CHANGE:
    navigate(CHANGE_DIAPER);
    break;
  case A_PUMP:
    navigate(snapshot["pump"].isNull() ? PUMP : PUMP_TIMER);
    break;
  case A_SLEEP:
    navigate(SLEEP);
    break;
  case A_LEFT:
  case A_RIGHT:
    obj["side"] = a == A_LEFT ? "left" : "right";
    submit("bf_start", obj);
    break;
  case A_BOTTLE:
    if (!bottleDraft) {
      amount = constrain((int)(snapshot["bottle_default_ml"] | 60.0), 5, 1000);
      amount = (amount + 2) / 5 * 5;
      bottleDraft = true;
    }
    navigate(BOTTLE);
    break;
  case A_SWITCH:
    action("bf_switch", "bf");
    break;
  case A_STOP_BF:
    action("bf_stop", "bf");
    break;
  case A_DELETE: {
    DynamicJsonDocument e(2048);
    deserializeJson(e, lastResultRow);
    if (submit("delete", obj, e.as<JsonObject>()).length())
      toast("Deleting...");
    break;
  }
  case A_DONE:
    navigate(HUB);
    break;
  case A_MINUS:
    if (screen == BOTTLE)
      amount = max(5, amount - 5);
    else
      pumpAmount = max(0, pumpAmount - 5);
    break;
  case A_PLUS:
    if (screen == BOTTLE)
      amount = min(1000, amount + 5);
    else
      pumpAmount = min(2000, pumpAmount + 5);
    break;
  case A_BREAST:
    formula = false;
    break;
  case A_FORMULA:
    formula = true;
    break;
  case A_SAVE_BOTTLE: {
    obj["ml"] = amount;
    obj["kind"] = formula ? "formula" : "breast_milk";
    String id = submit("bottle", obj);
    if (id.isEmpty())
      break;
    undoOpId = id;
    undoRow = "";
    bottleDraft = false;
    navigate(HUB);
    toast("Bottle " + String(amount) + " ml " + (formula ? "formula" : "breast milk") + " logged", true);
    break;
  }
  case A_WET:
    logDiaper(true, false);
    break;
  case A_DIRTY:
    logDiaper(false, true);
    break;
  case A_BOTH:
    logDiaper(true, true);
    break;
  case A_START_PUMP:
    action("pump_start");
    break;
  // Stop commits immediately. The next screen annotates the completed row, versioned.
  case A_STOP_PUMP:
    action("pump_stop", "pump");
    break;
  case A_SKIP_AMOUNT:
    navigate(HUB);
    break;
  case A_SAVE_AMOUNT: {
    DynamicJsonDocument e(2048);
    deserializeJson(e, lastResultRow);
    obj["total_ml"] = pumpAmount;
    submit("pump_amount", obj, e.as<JsonObject>());
    break;
  }
  case A_TAB_LOG:
    sleepTab = TAB_LOG;
    break;
  case A_TAB_STATS:
    sleepTab = TAB_STATS;
    break;
  case A_END_AUTO:
  case A_STOP_NAP:
    action("sleep_stop", "sleep");
    break;
  case A_DISMISS_AUTO:
    if (action("sleep_dismiss", "sleep").length())
      toast("Removed; it will not be re-detected");
    break;
  case A_START_NAP:
    action("sleep_start");
    break;
  case A_REFRESH:
    nextPoll = 0;
    navigate(HUB);
    break;
  case A_RETRY:
    retryAt = 0;
    clearNotice();
    navigate(HUB);
    break;
  case A_DISCARD:
    if (rejected.length()) {
      rejected = "";
      rejectedReason = "";
      clearNotice();
      nextPoll = 0;
      navigate(HUB);
      toast("Refused log discarded");
    } else if (timerPending.length() && retryAt == UINT32_MAX) {
      prefs.remove("timer");
      timerPending = "";
      retryAt = 0;
      clearNotice();
      nextPoll = 0;
      navigate(HUB);
      toast("Timer operation dropped");
    }
    break;
  case A_VOL:
    volIdx = (volIdx + 1) % 4;
    prefs.putInt("vol", volIdx);
    if (isMuted())
      stopSong();
    else
      playSong(SONG_SARIA); // audible confirmation at the new level
    break;
  case A_DASH_WORD:
    playSong(songForStatus(curStatus));
    break;
  case A_DASH_HOME:
    navigate(HUB);
    break;
  case A_DASH_TAP:
    if (page == PAGE_LAMP)
      lampPeekUntil = millis() + 4000;
    else if (curStatus == BS_CRYING && !cryAcked) {
      cryAcked = true;
      stopSong();
      buzz(25);
    } else
      navigate(HUB);
    break;
  }
  dirty = true;
}
// ---------------- touch ----------------
// Precedence: wake a dimmed screen, then the control under the finger. A control lights on press
// and fires on release. The press stays armed while the finger stays within CANCEL_SLOP of the
// control and re-arms if it comes back, which is what a phone does; the old rule demanded the
// release land within 12 px of the control and silently ate an ordinary thumb roll.
void serviceTouch() {
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    lastTouch = touchAt = millis();
    pressX = moveX = t.x;
    pressY = moveY = t.y;
    longFired = false;
    swallowTouch = false;
    nightWakeUntil = millis() + 15000;
    if (nightDimActive) {
      swallowTouch = true;
      armedId = 0;
      setPressed(0);
      dirty = true; // the screen comes back up out of the dim
    } else {
      armedId = hitAt(t.x, t.y);
      setPressed(armedId > 0 ? armedId : 0);
      if (armedId > 0)
        buzz(12);
    }
  }
  if (t.isPressed() && !swallowTouch && !longFired) {
    if (t.x >= 0 && t.y >= 0) {
      moveX = t.x;
      moveY = t.y;
    }
    if (screen == DASHBOARD && millis() - touchAt >= 600) {
      longFired = true;
      armedId = 0;
      setPressed(0);
      toggleLamp();
    } else if (armedId > 0)
      setPressed(stillOn(armedId, moveX, moveY) ? armedId : 0);
  }
  if (t.wasReleased()) {
    int id = armedId;
    armedId = 0;
    setPressed(0);
    if (swallowTouch || longFired)
      return;
    // Dashboard pages: a horizontal swipe moves status -> stats -> lamp -> status.
    if (screen == DASHBOARD && abs(moveX - pressX) > 70 && abs(moveY - pressY) < 60) {
      setPage((Page)(((int)page + (moveX < pressX ? 1 : 2)) % 3));
      buzz(12, 70);
      return;
    }
    if (id > 0 && stillOn(id, moveX, moveY))
      doAction(id);
  }
}
/** The three capacitive dots below the glass. M5Unified raises these for any touch at y >= 240,
 *  and still delivers that touch; hitAt refuses those rows so one contact cannot do two things. */
void serviceButtons() {
  if (M5.BtnA.wasPressed()) {
    if (nightDimActive)
      nightWakeUntil = millis() + 15000;
    else if (screen == DASHBOARD || screen == HUB)
      navigate(HUB);
    else
      navigate(backOf(screen));
  }
  if (M5.BtnB.wasPressed()) {
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
}
/** One input sample. Called at the top of the loop and again the instant a repaint finishes, so
 *  a frame's worth of pixels costs at most one missed sample rather than a whole gesture. */
void serviceInput() {
  M5.update();
  serviceTouch();
  serviceButtons();
#ifdef NURSERYPAD_PROFILE
  profSampled();
#endif
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
#ifdef NURSERYPAD_PROFILE
  Serial.begin(115200);
#endif
  M5.Display.setRotation(1);
  M5.Display.setBrightness(BRIGHT_DAY);
  brightness = BRIGHT_DAY;
  M5.Speaker.begin();
  M5.Power.setLed(0);
  prefs.begin("nurserypad", false);
  volIdx = prefs.getInt("vol", 2);
  caregiver = prefs.getString("caregiver", "");
  canvas.setColorDepth(16);
  // The frame lives in internal RAM when that leaves TLS and JSON their room, else in PSRAM.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < SCR_W * SCR_H * 2 + 140000)
    canvas.setPsram(true);
  if (!canvas.createSprite(SCR_W, SCR_H)) {
    canvas.setPsram(true);
    canvas.createSprite(SCR_W, SCR_H);
  }
  loadOutbox();
  setupM5GoLeds();
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
  render();
}
void loop() {
  serviceInput();
  serviceVibe();
  serviceSong();
  NetResult result;
  while (xQueueReceive(results, &result, 0) == pdTRUE)
    consume(result);
  if (!pending && WiFi.status() == WL_CONNECTED) {
    if ((int32_t)(millis() - nextPoll) >= 0) {
      request("device_snapshot", "{\"protocol\":1}");
      nextPoll = millis() + (screen == DASHBOARD && (curStatus == BS_SLEEPING || curStatus == BS_AWAY) ? 30000 : 15000);
    } else if (retryAt != UINT32_MAX && (int32_t)(millis() - retryAt) >= 0 && (timerPending.length() || queued)) {
      String envelope = timerPending.length() ? timerPending : outbox[0];
      request("device_op", "{\"envelope\":" + envelope + "}", true);
    } else if (statsWanted) {
      request("device_sleep_today", "{}");
      statsWanted = false;
    }
  }
  if (curStatus == BS_CRYING && sourceAge() < 180 && !sourceError.length() && !isMuted() && !cryAcked &&
      playingSong < 0 && page != PAGE_LAMP && (int32_t)(millis() - cryNextLoopMs) >= 0) {
    playSong(SONG_STORMS);
    cryNextLoopMs = millis() + 5500;
  }
  bool form = screen == BOTTLE || screen == CHANGE_DIAPER || screen == PUMP_AMOUNT || screen == FEED || screen == REVIEW;
  if (screen != HUB && screen != DASHBOARD && millis() - lastTouch > (form ? 120000UL : 45000UL))
    navigate(HUB);
  if (toastText.length() && (int32_t)(toastUntil - millis()) <= 0) {
    toastText = "";
    undoOpId = "";
    undoRow = "";
    dirty = true;
  }
  DisplayState ds = displayState();
  serviceM5GoLeds(ds, pending || queued || timerPending.length(), failed, WiFi.status() == WL_CONNECTED, inNightWindow());
  nightDimActive = screen == DASHBOARD && page != PAGE_LAMP && ds == DS_ASLEEP && inNightWindow() &&
                   (int32_t)(millis() - nightWakeUntil) >= 0 && sourceAge() < 900 && !sourceError.length();
  uint8_t want = screen == DASHBOARD && page == PAGE_LAMP ? BRIGHT_LAMP
                 : nightDimActive                         ? BRIGHT_NIGHT
                 : ds == DS_CRYING                        ? BRIGHT_ALERT
                                                          : BRIGHT_DAY;
  if (want != brightness) { // the PMIC is on the touch controller's I2C bus: write only on change
    brightness = want;
    M5.Display.setBrightness(want);
  }
  bool animating = ds == DS_CRYING && (screen == DASHBOARD ? page == PAGE_STATUS && !nightDimActive : !cryAcked);
  if (dirty || (int32_t)(millis() - nextDraw) >= 0) {
    render();
    nextDraw = millis() + (animating ? 120 : 500);
    serviceInput(); // a full frame is the longest the panel goes unwatched; sample the moment it ends
  } else if (dirtyRect) {
    renderRect(dirtyX, dirtyY, dirtyW, dirtyH);
    serviceInput();
  }
  delay(2); // the touch controller will not report faster than every 4 ms; match it
}
