#pragma once
#include <Arduino.h>
enum Screen {
  HUB,
  FEED,
  FEED_TIMER,
  FEED_DONE,
  BOTTLE,
  CHANGE_DIAPER,
  PUMP,
  PUMP_TIMER,
  PUMP_AMOUNT,
  SLEEP,
  REVIEW,
  DASHBOARD
};
enum SleepTab { TAB_LOG, TAB_STATS };
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
// Hit-area ids. 0 means "nothing"; -1 (from hitAt) means a disabled control swallowed the tap.
enum Act {
  A_NONE = 0,
  // chrome
  A_BACK,
  A_DASH,
  A_CAREGIVER,
  A_NOTICE,
  A_STRIP_OPEN,
  A_STRIP_STOP,
  A_UNDO,
  A_SILENCE,
  // hub
  A_FEED,
  A_CHANGE,
  A_PUMP,
  A_SLEEP,
  // feed
  A_LEFT,
  A_RIGHT,
  A_BOTTLE,
  A_SWITCH,
  A_STOP_BF,
  A_DELETE,
  A_DONE,
  // steppers and bottle
  A_MINUS,
  A_PLUS,
  A_BREAST,
  A_FORMULA,
  A_SAVE_BOTTLE,
  // change
  A_WET,
  A_DIRTY,
  A_BOTH,
  // pump
  A_START_PUMP,
  A_STOP_PUMP,
  A_SKIP_AMOUNT,
  A_SAVE_AMOUNT,
  // sleep
  A_TAB_LOG,
  A_TAB_STATS,
  A_END_AUTO,
  A_DISMISS_AUTO,
  A_START_NAP,
  A_STOP_NAP,
  // review
  A_REFRESH,
  A_RETRY,
  A_DISCARD,
  // dashboard
  A_VOL,
  A_DASH_HOME,
  A_DASH_TAP
};
