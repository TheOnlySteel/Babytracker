// Shared types for CradleWatch. These live in a header (not the .ino) because
// the Arduino builder hoists auto-generated function prototypes above type
// definitions in the sketch, which breaks any function signature using them.
#pragma once
#include <Arduino.h>

// Baby status values from GET /baby/status
enum BabyStatus { BS_NONE, BS_SLEEPING, BS_AWAKE, BS_STIRRING, BS_CRYING, BS_AWAY };

// What the screen shows (sleeping splits into settled green / settling yellow)
enum DisplayState { DS_BOOT, DS_ASLEEP, DS_SETTLING, DS_STIRRING, DS_AWAKE, DS_CRYING, DS_AWAY };

enum SongId { SONG_LULLABY, SONG_NOCTURNE, SONG_SUNS, SONG_STORMS, SONG_SARIA, SONG_COUNT };

enum Page { PAGE_STATUS, PAGE_STATS, PAGE_LAMP };

struct Note {
  uint16_t freq;
  uint16_t ms;
  uint16_t gapMs;
}; // gapMs = silence after the note
struct Song {
  const Note *notes;
  uint8_t len;
};

struct DayMetrics {
  bool haveAny = false;
  String soothes, rise, bed, naps, longest, inBed, awakeInBed; // display_value strings
  int bedMin = -1, riseMin = -1; // parsed clock minutes for night mode
};

struct Box {
  int x, y, w, h;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
