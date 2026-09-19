/* NurseryPad protocol 1. UI runs on Arduino's core; HTTPS runs on core 0.
 * Pair in the web app. No Cradlewise token belongs on this device.
 *
 * Screens follow the design canvas: a hub of four tiles with the crib-state circle top right,
 * one-tap forms, big timers, a timer strip along the bottom, and Dashboard mode (CradleWatch)
 * reached from the circle on any screen. Layout constants live in ui.h.
 */
#include "app.h"
