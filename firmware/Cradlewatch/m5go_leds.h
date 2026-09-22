#pragma once

// M5GO Battery Bottom 2: ten SK6812/WS2812-compatible LEDs on GPIO 25.
// Rendering is deliberately capped and non-blocking: nursery light, not disco.
constexpr uint8_t M5GO_LED_PIN = 25;
constexpr uint8_t M5GO_LED_COUNT = 10;
CRGB m5goLeds[M5GO_LED_COUNT];
uint32_t nextLedFrame = 0;

CRGB statusLedColour(DisplayState state) {
  switch (state) {
  case DS_ASLEEP: return CRGB(40, 128, 72);
  case DS_SETTLING:
  case DS_STIRRING: return CRGB(248, 184, 40);
  case DS_AWAKE: return CRGB(216, 112, 32);
  case DS_CRYING: return CRGB(184, 48, 48);
  case DS_AWAY: return CRGB(48, 80, 112);
  default: return CRGB(72, 64, 88);
  }
}

void setupM5GoLeds() {
  FastLED.addLeds<WS2812, M5GO_LED_PIN, GRB>(m5goLeds, M5GO_LED_COUNT);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 300);
  FastLED.clear(true);
}

void serviceM5GoLeds(DisplayState state, bool isPending, bool isFailed, bool wifiOk,
                     bool night) {
  uint32_t now = millis();
  if ((int32_t)(now - nextLedFrame) < 0)
    return;
  nextLedFrame = now + 40; // 25 fps; FastLED.show never runs in the network task
  fill_solid(m5goLeds, M5GO_LED_COUNT, CRGB::Black);

  uint8_t phase = (now / 80) % (M5GO_LED_COUNT * 2 - 2);
  uint8_t pos = phase < M5GO_LED_COUNT ? phase : M5GO_LED_COUNT * 2 - 2 - phase;
  if (isFailed || !wifiOk) {
    // A moving amber fairy is visible without flooding a dark room.
    m5goLeds[pos] = isFailed ? CRGB(184, 48, 48) : CRGB(248, 184, 40);
  } else if (isPending) {
    m5goLeds[pos] = CRGB(248, 200, 80);
    m5goLeds[(pos + M5GO_LED_COUNT - 1) % M5GO_LED_COUNT] = CRGB(72, 48, 24);
  } else {
    CRGB colour = statusLedColour(state);
    uint8_t wave = sin8((now / 12) & 0xff);
    uint8_t level = night ? scale8(wave, 10) + 2 : scale8(wave, 30) + 8;
    if (state == DS_CRYING)
      level = ((now / 300) & 1) ? 72 : 10;
    colour.nscale8_video(level);
    fill_solid(m5goLeds, M5GO_LED_COUNT, colour);
  }
  FastLED.setBrightness(night ? 28 : 72);
  FastLED.show();
}
