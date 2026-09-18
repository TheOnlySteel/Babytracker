# NurseryPad

M5Stack Core2 firmware for the BabyTracker integration. All device RPCs are POST requests to Supabase. The device has no Cradlewise credential and never calls Cradlewise directly.

## Compile

Pinned versions (also in `.github/workflows/firmware.yml`):

- arduino-cli 1.3.1
- esp32:esp32 3.3.1
- M5Unified 0.2.22
- M5GFX 0.2.29
- ArduinoJson 6.21.5

```sh
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.1
arduino-cli lib install M5GFX@0.2.29 M5Unified@0.2.22 ArduinoJson@6.21.5
cp NurseryPad/secrets.example.h NurseryPad/secrets.h
# Fill the ignored file with the pairing key, public anon key, Wi-Fi, URL and timezone.
arduino-cli compile --fqbn esp32:esp32:m5stack_core2 NurseryPad
```

The bundled public trust anchors are GTS Root R4 and ISRG Root X1. Certificate validation is mandatory. Verify the actual project chain from a board before using the monitor.

## Controls

A / top-left: Home/back. B: Home. C / crib circle: Dashboard. A short dashboard tap returns home; a 600 ms hold toggles lamp mode. The top-left speaker cycles volume. A tap first wakes a dimmed screen or silences an active cry; that tap does not also navigate. Forms retain their amount/toggle drafts when they time out to Home.

The caregiver chip cycles the household's caregivers. It controls attribution; the device key controls authentication. Settings controls the device's default boot mode; a local dashboard/lamp toggle persists until that server setting changes.

Stop saves the timer immediately. Feed Done provides versioned Delete or Done. After stopping a pump, enter its total amount; saving that amount updates the completed row without changing the stop time. Sleep End locks automatic timing; Not a nap keeps the source-key tombstone.

Bottle/diaper logs persist before “Saved locally” appears. A new bottle form starts with the snapshot's recent bottle amount; polling and returning Home preserve an existing draft until it is saved locally. Eight logs can wait offline; a ninth is refused. No write is accepted before NTP has synchronized since boot. Timer operations require a recent snapshot and an empty one-shot queue. An uncertain timer request is retained with its operation ID until a definitive response, including across reboot. This is retry protection for one operation, not an offline timer chain.

Failed logs remain visible. Tap the warning to refresh/retry the same operation. A definitive timer conflict can be acknowledged after reviewing the current log; a one-shot log is removed only after `applied` or `duplicate`. If stored queue data is malformed, logging stops so it cannot overwrite the unresolved queue.

The source's observation timestamp controls stale warnings, even when snapshots are fresh. Past 15 minutes (or a source error), the lamp is neutral. “Settled” uses observed crib states, not editable sleep-entry duration.

## Physical release gate

Compilation does not verify touchscreen coordinates, sound level, Wi-Fi/certificate behavior, flash durability or concurrent use by two real devices. Execute the checks in `docs/implementation-handoff.md` before replacing the live nursery monitors.

## Status and known gaps (2026-09-18)

Reviewed and corrected before commit: the response buffer is no longer read after it is freed; a
refused one-shot log is parked for review instead of wedging the queue, transient failures back
off (15 s → 5 min) instead of stopping, timer buttons stay disabled until a snapshot newer than
the last timer op arrives, bottle and diaper logs may be queued before NTP sync (the server
stamps them at receipt and marks them `time_uncertain`), and the stale banner reads
"unavailable" until the first observation.

Still to do, in a follow-up, before this replaces the standalone CradleWatch unit:

- Bring the screens to the agreed design: 2×2 hub tiles with the crib-state circle top-right,
  the timer strip along the bottom edge with proper labels, Change as three one-tap columns with
  an undo toast, button A as Back.
- Restore CradleWatch behaviours the fork dropped: lamp tap-to-peek, tap-the-word chime replay,
  the volume blip, the daytime calm dim, the boot screen, a Wi-Fi-lost indication on pad screens.
- Bundle GTS Root R1 next to R4 and ISRG X1 after confirming the chain the board actually sees
  (`openssl s_client -connect <project>.supabase.co:443 -showcerts`); keep the CA that last
  succeeded instead of retrying from the first on every request.
- Store the outbox as per-slot keys or bytes rather than one NVS string.
- Physical checks on two units per `docs/rollout.md` step 10. A CI compile is not a hardware test.
