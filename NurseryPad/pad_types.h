#pragma once
#include <Arduino.h>
enum Screen {
  HUB,
  FEED,
  FEED_TIMER,
  FEED_DONE,
  BOTTLE,
  DIAPER_SCREEN,
  PUMP,
  PUMP_TIMER,
  PUMP_AMOUNT,
  SLEEP_LOG,
  SLEEP_STATS,
  DASHBOARD
};
struct NetCommand {
  char rpc[32];
  char body[1536];
  bool operation;
};
struct NetResult {
  int code;
  bool operation;
  bool stats;
  char *body;
};
struct PadButton {
  int x, y, w, h, action;
  bool enabled;
};
