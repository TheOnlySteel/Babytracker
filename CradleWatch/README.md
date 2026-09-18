# CradleWatch

A glanceable baby sleep dashboard for the **M5Stack Core2**, driven by the
[Cradlewise Data API](https://integrations.cradlewise.com/documentation)
(read-only, beta, requires a Nurture Plus subscription).

The whole screen becomes the baby's status color, readable across a dark room:

| Screen | Meaning |
| --- | --- |
| 🟢 Green — **Asleep** | asleep and settled for over 10 minutes |
| 🟡 Yellow — **Settling** / **Stirring** | just fell asleep, or stirring |
| 🟠 Orange — **Awake** | awake |
| 🔴 Red — **Crying** | pulsing ring, full brightness, looping chime |
| 🌑 Grey-blue — **Not in crib** | baby is out of the crib |

Every state change plays a short square-wave chime — Ocarina of Time motifs:

| State | Chime |
| --- | --- |
| Asleep | Zelda's Lullaby |
| Stirring | Nocturne of Shadow |
| Awake | Sun's Song |
| Crying | Song of Storms (loops every few seconds until tapped or muted) |
| Not in crib | Saria's Song |

## Controls

| Where | Action |
| --- | --- |
| Top-right speaker icon | volume: tap cycles mute → low → medium → high (waves show the level; a blip plays at the new volume; saved to flash) |
| Tap the state name | replay the current state's chime |
| Tap anywhere during crying | silence the looping chime until the next cry |
| Tap elsewhere, or any button | cycle status → stats → lamp (the page survives reboots) |
| On the lamp page | tap to peek at the numbers for 10 s; tap again to move on |

The stats page shows today's seven key metrics (rise time, bedtime, longest
stretch, naps, soothes, awake in bed, time in bed) from the day-metrics
endpoint, refreshed every 30 minutes.

**Lamp page (night score)**: a third page for the off-shift parent — the
whole screen becomes one constant colored glow showing how the night has
gone so far: percentage of the night (since bedtime) spent asleep, where
sleeping *and stirring* count as good and only awake, crying, and away
count against. ≥85% glows deep green, sliding through amber to a soft
ember at 50% and below. It never flashes, never pulses, and plays **no
chimes** — the on-shift parent is handling things; the lamp just tells
you whether to go help. Tap once to peek at the numbers (score, asleep
vs. awake time, wakings); the first 45 minutes of the night show a
neutral glow while the sample is still too small to be meaningful. After
a mid-night reboot the score is rebuilt from the sleep-sessions endpoint.

**Night mode**: while the baby is asleep between bedtime and rise time
(learned from the day's metrics, defaulting to 8:30 pm–7:00 am), the screen
drops to a faint green glow. Any tap wakes it for 15 seconds. Chimes duck
to 40% of the chosen volume during the night window. Crying overrides
everything: full brightness, flashing, looping chime.

**Power**: Wi-Fi modem sleep, an 80 MHz CPU clock, a 1 fps idle redraw
(12 fps only while the crying ring animates), and the power LED off keep
the average draw down; the screen also half-dims during settled green
sleep when nobody has touched it for 15 seconds (any tap restores full
brightness). Expect roughly 5-7 hours on the Core2's battery — this is
still a device that wants to live on USB-C, with the battery covering
outages and room-to-room moves.

If polling fails (Wi-Fi drop, API error), a banner shows how old the data is
rather than silently displaying a stale status. An expired token (they last
60 days) shows its own banner — regenerate on the Cradlewise web dashboard.

## Setup

1. On the [Cradlewise web dashboard](https://cradlewise.com), go to
   **Cradlewise Data API** and press **Generate** to create a token
   (admin account + active Nurture Plus required).
2. Copy `secrets.example.h` to `secrets.h` and fill in your Wi-Fi
   credentials, the token, and your timezone. `secrets.h` is gitignored.
3. Build and upload with the Arduino IDE (or `arduino-cli`):
   - **arduino-esp32** core, board **M5Core2**
   - Library: **M5Unified** (pulls in M5GFX)
4. That's it — no other libraries. HTTPS roots (Amazon Root CA 1 and ISRG
   Root X1) are bundled in `cradlewise_certs.h`; the sketch tries each.

## Polling & rate limits

The Data API allows 2 status requests/minute and 2,880 requests/day total —
exactly 24 h of 30-second polling with nothing left over. CradleWatch
therefore polls adaptively: every 30 s while things are happening (settling,
stirring, awake, crying), every 60 s during settled sleep and while the baby
is away. That leaves headroom for the day-metrics fetch (every 30 min) while
staying at most-responsive exactly when it matters. `429` responses honor
`Retry-After`; transport errors back off exponentially to 5 minutes.

## Notes

- The chime melodies are Nintendo compositions (Koji Kondo), rendered as
  six-note tone tables for a personal device — this is a hobby project, not
  something to redistribute as a product.
- The status endpoint reflects "standard backend latency" per the API docs;
  expect the screen to trail reality by a poll interval or two.
