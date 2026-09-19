# NurseryPad

M5Stack Core2 firmware for the BabyTracker integration. All device RPCs are POST requests to Supabase. The device has no Cradlewise credential and never calls Cradlewise directly.

## Compile and flash

Pinned versions (also in `.github/workflows/firmware.yml`):

- arduino-cli 1.3.1 (or the Arduino IDE with the same board and library versions)
- esp32:esp32 3.3.1, board **M5Stack Core2**
- M5Unified 0.2.22
- M5GFX 0.2.29
- ArduinoJson **6.21.5** (the sketch uses the version 6 API; version 7 does not build it)
- FastLED 3.10.3 (the M5GO Bottom2 LEDs)

```sh
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.1
arduino-cli lib install M5GFX@0.2.29 M5Unified@0.2.22 ArduinoJson@6.21.5 FastLED@3.10.3
cp NurseryPad/secrets.example.h NurseryPad/secrets.h   # fill in; the copy is git-ignored
arduino-cli compile --fqbn esp32:esp32:m5stack_core2 NurseryPad
arduino-cli upload  --fqbn esp32:esp32:m5stack_core2 -p /dev/cu.usbserial-XXXX NurseryPad
```

`secrets.h` needs the Wi-Fi credentials, the project URL (`https://<ref>.supabase.co`), the public anon key, a device key from Settings → Pair device in the web app (shown once), and the POSIX `TZ_STRING`.

The bundled public trust anchors are GTS Root R4 and ISRG Root X1. Certificate validation is mandatory. Verify the actual project chain from a board before using the monitor.

## Files

- `NurseryPad.ino` is a stub that includes `app.h`. Keeping the program in a header means the Arduino builder never generates prototypes for it, so it builds the same under the IDE, arduino-cli and CI.
- `ui.h`: design tokens (palette, type scale, spacing) taken from the design canvas at half scale, text and widget helpers, hit-area registry, icons, formatting.
- `dashboard.h`: chimes, haptics, derived crib state, and the Dashboard-mode renderers (status, dimmed night, lamp), from CradleWatch.
- `m5go_leds.h`: the ten LEDs in the M5GO Battery Bottom 2 on GPIO 25, driven with FastLED at a capped, non-blocking 25 fps.
- `app.h`: state, transport task, write queue, response handling, the pad screens, input, `setup()` and `loop()`.

## Screens

Every pad screen has the same 36 px top bar: Back (except on the hub), the title (the child's name on the hub), the clock, the caregiver chip (tap to switch who is logging) and the crib-state circle. **The circle opens Dashboard mode from any screen.** A red dot beside the clock means writes are waiting to sync.

- **Hub**: one line of state (crib, last feed, last change) and four tiles: FEED, CHANGE, PUMP, SLEEP, each with its most recent event.
- **Feed**: LEFT and RIGHT start a breastfeed; the BOTTLE row below opens the bottle form. The timer shows the active side, the running total and per-side totals, with SWITCH and STOP. Stop commits; the confirmation shows the totals with Delete (versioned) and DONE.
- **Bottle**: minus / amount / plus in **5 ml** steps, Breast milk or Formula, SAVE. Starts from the last bottle's amount.
- **Change**: WET, DIRTY, BOTH. One tap saves and returns to the hub with an UNDO toast (5 s). The most recent kind is highlighted with its age.
- **Pump**: START PUMP; the timer has STOP; the amount screen asks for the total in 5 ml steps with Skip and SAVE.
- **Sleep**: LOG and STATS tabs. Log shows the automatic crib nap (End now, Not a nap) or START NAP / STOP NAP for naps outside the crib, then today's sleeps. Stats is a 3×2 grid: total sleep, naps, longest nap, night sleep, wakings, awake in bed, with rise and bed times and the crib data age.
- **Timer strip**: while a breastfeed, pump or nap runs and its own screen is not open, a strip along the bottom shows it with a STOP button; tapping the strip opens the timer.
- **Review**: reached by tapping an amber notice line. Shows a refused log or a timer operation that needs a decision, with Refresh, Retry and Discard.
- **Crying**: on any pad screen a live crying state takes over the screen with the pulsing ring until tapped once; the chime loops until then.

**Dashboard mode** is the CradleWatch screen: the whole display is the crib state with the word, how long, and the day's log line along the bottom. Speaker top-left cycles the volume and plays a confirmation motif at the new level; the home circle top-right returns to the hub; tapping the state word replays its chime; a tap anywhere else also returns home; a 600 ms hold toggles the lamp. A horizontal swipe moves between the three dashboard pages, status, today's sleep numbers, and the lamp, shown by the dots at the bottom. On the lamp a tap peeks at the numbers and a hold returns to the status page. Settled night sleep in the bed-to-rise window dims to the night screen after 15 s without touch.

**Lights.** The Bottom2's ten LEDs breathe in the crib state's colour, low and slow, capped lower in the night window with a 300 mA power ceiling. Pending writes chase in gold, Wi-Fi loss shows a moving amber point, and a device error shows red.

Physical buttons: A is Back, B is Home, C is Dashboard. Sub-screens return to the hub after 45 s idle (2 min on forms, drafts kept); Dashboard never times out.

## Input model

Every tappable element registers two rectangles while it is drawn: the one it drew, and that one grown to 44 px. A press inside a drawn rectangle takes it outright; a near miss goes to the nearest centre rather than to whatever was drawn last. The top bar and the timer strip own their rows, and nothing drawn in the body may grow into them, so a short control sitting flush under the bar cannot steal the Back chevron's lower half. Touches at y >= 240 are left to buttons A, B and C, which is what M5Unified raises there; they never also fire a screen control.

A press highlights the element and buzzes. It stays armed while the finger stays within 22 px of the element and re-arms if the finger comes back, so an ordinary thumb roll no longer eats the tap; it fires on release and is abandoned if the element has left the screen meanwhile. A press on a disabled control is swallowed. Precedence: waking a dimmed screen, then the element under the finger.

The frame is one full-screen sprite, in internal RAM when that leaves TLS and JSON their room, else in PSRAM. A change confined to one control repaints and transfers only that rectangle, since `pushImage` clips before it transfers, and the stepper's plus and minus mark just their own row rather than the whole frame; a full 320x240 push is about 31 ms of SPI time by itself, and the panel cannot see the finger while it happens. Input is sampled at the top of the loop and again the instant a repaint ends, so a frame costs at most one sample rather than a gesture. Build with `-DNURSERYPAD_PROFILE` to print frame times and the worst input gap to serial at 115200. Brightness is written to the PMIC only when it changes, since it shares the touch controller's I2C bus.

## Behaviour that matters

Stop saves the timer immediately. Feed Done offers versioned Delete or Done. After stopping a pump, enter its total amount; saving updates the completed row without changing the stop time. Sleep End locks automatic timing; Not a nap keeps the source-key tombstone.

Bottle and diaper logs persist locally before the toast appears and flush in order; eight can wait offline and a ninth is refused. UNDO on the toast removes the log from the queue if it has not been sent, or deletes the row the server created if it has. No timer operation is accepted before NTP has synchronized since boot; bottle and diaper logs may be queued before that and are timestamped at receipt with `time_uncertain`. Timer operations require a recent snapshot and an empty one-shot queue. An uncertain timer request is retained with its operation ID until a definitive response, including across reboot. A refused one-shot log is parked for review so the logs behind it still flush; transient failures back off from 15 s to 5 min.

The caregiver chip controls attribution; the device key controls authentication. The source's observation timestamp drives stale warnings; past 15 minutes the crib circle goes hollow and the lamp goes neutral. "Settled" uses observed crib states, not editable sleep-entry duration.

## Physical release gate

Compilation does not verify touchscreen coordinates, sound level, Wi-Fi and certificate behaviour, flash durability or concurrent use by two real devices. Execute the checks in `docs/rollout.md` step 10 before replacing the live nursery monitors.

## Status and known gaps (2026-09-19)

The 2026-09-18 build had 32 px targets, actions on press without feedback, three type sizes on one screen and a bottom message banner that hid content; it was replaced by the screens above, which follow the design canvas (https://claude.ai/artifact/97CjRPpawvbcUEZ2h5q6he) at half scale. The screens that replaced it still resolved a touch by draw order, which let the body's first control take the bottom rows of the top bar: on Sleep the tab strip owned 14 of the bar's 36 rows, so the lower half of the Back chevron selected a tab and the crib circle opened Stats. The input model above is the fix. Verified here: a clean compile with all warnings on, and the hit table simulated off the shipped `hit`/`hitAt` code to confirm the bar keeps all 44 of its rows on every screen. Not verified here: anything on a board, including the frame times the profile build reports. Still to do:

- The feed screen cannot suggest the next side; the snapshot does not carry the last side yet.
- Restore the remaining CradleWatch niceties the fork dropped: the daytime calm dim and the boot screen.
- Bundle GTS Root R1 next to R4 and ISRG X1 after confirming the chain the board actually sees (`openssl s_client -connect <project>.supabase.co:443 -showcerts`); keep the CA that last succeeded instead of retrying from the first on every request.
- Store the outbox as per-slot keys or bytes rather than one NVS string.
- Measure the frame with the profile build before deciding whether the sprite is worth keeping. Drawing straight to the panel, with a sprite only for the lamp gradient and the crying overlay, is the remaining structural win and would retire the PSRAM question.
- Physical checks on two units per `docs/rollout.md` step 10.
