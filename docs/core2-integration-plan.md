# Core2 Nursery Pad and Sleep — implementation plan (v2)

> **Status 2026-09-18:** implemented on this branch through release F (backend collection, sleep on the phones, read-only pad and device writes), with derivation off and nothing deployed. `docs/rollout.md` is the operating checklist; `docs/cradlewise-data-api.md` is the API reference the parsers follow. The bullets below remain the design record.

Written 2026-09-17, revised 2026-09-18 after the Codex review in `docs/review-2026-09-17/` (thirteen findings, an implementation proposal and an acceptance checklist). Baseline is Babytracker `main` at 458e012, which carries the CradleWatch sketch under `CradleWatch/`. Design mockup: the "Core2 Nursery Pad" canvas (tap-through prototype plus twelve static screens).

## 0. Decisions

| Topic | Decision |
| --- | --- |
| Where Cradlewise is polled | Supabase, by a scheduled Edge Function behind a scheduler secret. The Core2 stops talking to Cradlewise. Old direct-polling monitors are stopped or reflashed before the server poller is enabled, so exactly one client spends the token's budget. |
| Device transport | PostgREST RPC with the anon key plus a per-device secret header. All device functions are `POST`. No Edge Function on the device path. |
| Device resting screen | Home hub. Dashboard (CradleWatch) is a mode entered by the crib-state circle top-right, from any screen; tap anywhere in it returns to the hub. A unit can be configured to boot into dashboard or lamp so a bedside lamp stays a lamp after a reboot. |
| Sleep in the data model | New `entry_type` value `sleep`. Running sleep = `ended_at IS NULL`. No separate nap table. |
| Three kinds of sleep information | **Observations** (what Cradlewise said, when), **entries** (the editable log), and the **device snapshot** (a read model). They are stored and reasoned about separately. |
| Auto vs manual | Cradlewise-derived sleeps are `sleep` entries with `source = 'cradlewise'`, keyed by `(household_id, source_key)`. Live derivation makes them **provisional**; reconciliation against Cradlewise's own sleep history (`/sleep/c-chart`) confirms or adjusts them. Manual sleeps have `source = 'manual'`. |
| Ownership of an auto row | Annotations (note, place, kind) never take timing away from the poller. Only an explicit timing change (start, end, End now, Stop) sets `timing_locked`, after which the poller and reconciliation leave the row's times alone. |
| Attribution and provenance | A new `entries.via` column records the channel (`web`, `core2:<device>`, `cradlewise`, `nara`). Auto rows have `created_by = null`. Device taps carry the caregiver chosen on the device. |
| Timer semantics | The running entry in Postgres is the single source of truth. Phone and pad both read it, both can stop it, conditional on `updated_at`. Stop commits immediately on every surface; what follows is a confirmation, with an explicit delete path, never an implied unsaved draft. |
| Offline on the device | First write release queues one-shot logs only (bottle, diaper), with the original occurrence time. Timers require connectivity. Offline start/switch/stop chains are a later release with their own replay rules. |

Rejected alternative, kept for the record: the Core2 keeps polling Cradlewise and reports transitions upward. It preserves the current 30 s cry latency but makes nap logging depend on a powered device, doubles credentials on the ESP32, and makes a second monitor a budget problem. If the extra ~30 s on cry alerts matters in practice, this hybrid is the fallback; the schema and web app would not change.

## 1. Disposition of the review findings

| # | Finding | Disposition |
| --- | --- | --- |
| 1 | Fresh snapshots can hide a dead feed | **Accepted.** Snapshot carries `source_observed_at`, `source_error`, `snapshot_at` separately. Every surface shows crib-data age past 3 min; lamp and dimmed night screen go neutral; the deriver never bridges an observation gap. |
| 2 | Offline ops lack time, identity, dependencies | **Accepted.** Operation envelope with `occurred_at` and `clock_ok`; one-shot logs only in the first offline release; queue-full refuses visibly; Discard of a committed feed is a versioned delete, not the 30 s undo. |
| 3 | `edited` conflates annotation with timing ownership | **Accepted.** Split into `timing_locked`; conditional updates from the poller; explicit policy for a manual timer overlapping detection (§3.4). |
| 4 | Status polling alone is lossy | **Accepted.** Combined non-sleep debounce; gaps close a sleep at the last observation and flag it; c-chart reconciliation moves into the first sleep release. |
| 5 | Authorization boundary incomplete | **Accepted.** RLS on every new table, `REVOKE … FROM PUBLIC`, helpers in a private schema, scheduler secret, every RPC target scoped to the authenticated device's household, negative tests. |
| 6 | GET snapshot, enum in one transaction, partial-index conflict target | **Accepted.** POST everywhere; enum value in its own migration; plain unique constraint on `(household_id, source_key)`; canonical UTC second-precision keys. |
| 7 | Budget measured against the wrong window | **Accepted.** Rolling request ledger with atomic admission, a poll lease, response headers honoured, 401 and 403 distinct, metrics deferred before status. |
| 8 | Sleep totals, nights and wakings | **Accepted.** Interval overlap for totals; `night_key` groups a night across midnight; "Last night" view; the lamp's percentage is named "crib settled" and not equated with sleep minutes; household IANA timezone column. |
| 9 | JSON picker and blocking transport | **Accepted.** ArduinoJson with a validated schema and protocol version; network on a second FreeRTOS task with a result queue; `updated_at` handled as an opaque string. |
| 10 | Phone pump path doesn't meet the device contract | **Accepted.** Pump timer mode and banner on the phone ship before device pump writes; `total_ml` added to the pump payload; sheets preserve unknown payload keys. |
| 11 | Snapshot and controls don't cover the screens | **Accepted with cuts.** Seven-day stats dropped from the device; today's sleep list via a second bounded RPC; Stats cells defined; C button is Dashboard only; touch precedence defined; per-device boot mode. |
| 12 | Compatibility paths and rollout order | **Accepted.** Every type dispatcher audited with fixtures; store catch-up on foreground; harness committed to the repo; additive migrations → readers deployed → derivation enabled by flag. |
| 13 | `secrets.h` not ignored | **Fixed** in this revision (`.gitignore`). |

Where I hold a different view: the review's egress figure (≈780 MB/month at 1.5 kB per snapshot for three devices at 15 s) is closer than my earlier one and still a sixth of the free allowance, so it does not drive design; dashboard mode drops to 30 s anyway. And the Summary should keep reporting a sleep's wall-clock length as its duration, the way every tracker does, with Cradlewise's "awake in bed" shown beside it rather than subtracted from it. Everything else in the review I would build as written.

## 2. Architecture

```
Cradlewise Data API ──(cron, 30 s, scheduler secret)──▶ Edge Function cradlewise-poll
                                                          │  lease + rolling quota ledger
                                                          │  sleep_observations (every poll)
                                                          │  sleep_status (current row)
                                                          │  live derivation → provisional sleep entries
                                                          │  hourly + daily c-chart reconciliation
                                                          ▼
                                                     Supabase Postgres ◀──── web app (supabase-js, RLS, realtime)
                                                          ▲
                                                          │  POST rpc/device_snapshot, device_op, device_sleep_today
                                                          │  apikey: anon · x-device-key: per device
                                                     Core2 units (1..n)
```

## 3. Database

Three migrations, applied in order with `apply_migration` as before. Each is additive; nothing here changes existing rows.

### 3.1 `0004_sleep_enum.sql`

```sql
alter type entry_type add value if not exists 'sleep';
```

Alone in its migration so the value is committed before anything references it.

### 3.2 `0005_sleep.sql`

```sql
alter table households add column timezone text not null default 'America/Los_Angeles';

alter table entries
  add column source_key text,                 -- 'cw:<since as UTC, second precision>'
  add column via text not null default 'web', -- web | core2:<device> | cradlewise | nara
  add constraint entries_household_source_key_key unique (household_id, source_key);
update entries set via = 'nara' where nara_activity_key is not null;

create unique index entries_one_running_sleep on entries (household_id)
  where type = 'sleep' and ended_at is null and deleted_at is null;

create table sleep_observations (             -- one row per successful poll
  id bigserial primary key,
  household_id uuid not null references households on delete cascade,
  status text not null,                        -- sleeping|awake|stirring|crying|away|unknown
  since timestamptz,
  bounce text, music text,
  upstream_at timestamptz,                     -- response timestamp if the API gives one
  observed_at timestamptz not null,
  raw jsonb
);
create index sleep_observations_hh_at on sleep_observations (household_id, observed_at desc);

create table sleep_status (                    -- current state, realtime-published
  household_id uuid primary key references households on delete cascade,
  status text not null default 'unknown',
  since timestamptz,
  bounce text, music text,
  observed_at timestamptz,                     -- last successful poll
  source_error text,                           -- null | token_expired | forbidden | rate_limited | upstream | transport
  bed_min int, rise_min int,
  day_metrics jsonb,
  derive_enabled boolean not null default false,
  lease_until timestamptz,
  updated_at timestamptz not null default now()
);
create table cw_requests (                     -- rolling quota ledger
  id bigserial primary key,
  household_id uuid not null,
  endpoint text not null,
  at timestamptz not null default now(),
  ok boolean
);
create index cw_requests_hh_at on cw_requests (household_id, at desc);

alter table sleep_observations enable row level security;
alter table sleep_status enable row level security;
alter table cw_requests enable row level security;
create policy "sleep_observations select" on sleep_observations for select using (household_id = my_household_id());
create policy "sleep_status select" on sleep_status for select using (household_id = my_household_id());
alter publication supabase_realtime add table sleep_status;
create trigger sleep_status_set_updated_at before update on sleep_status for each row execute function set_updated_at();
```

Sleep payload contract (`SleepPayload` in `types.ts`):

```ts
{ kind: 'nap' | 'night', source: 'cradlewise' | 'manual',
  provisional?: boolean,      // live-derived, not yet reconciled
  timing_locked?: boolean,    // a human set or ended the times; poller and reconciliation leave them
  uncertain_end?: boolean,    // closed at the last observation before a gap
  night_key?: string,         // 'YYYY-MM-DD' of the evening the night began
  place?: 'crib' | 'stroller' | 'car' | 'arms' | 'other' }
```

Pump payload gains `total_ml?: number` for a volume with no side split; `pumpTotalMl` becomes `total_ml ?? left + right`.

### 3.3 `0006_devices.sql`

```sql
create schema if not exists device;             -- helpers and logs live here, never exposed
revoke all on schema device from public;

create table devices (
  id uuid primary key default gen_random_uuid(),
  household_id uuid not null references households on delete cascade,
  name text not null,
  key_hash bytea not null,
  boot_mode text not null default 'hub' check (boot_mode in ('hub','dashboard','lamp')),
  last_seen_at timestamptz,
  revoked_at timestamptz,
  created_at timestamptz not null default now()
);
create table device.ops (                       -- idempotency and audit
  device_id uuid not null references devices on delete cascade,
  client_op_id uuid not null,
  op text not null,
  request_hash bytea not null,                  -- reject reuse of an id with a different body
  result jsonb not null,
  entry_id uuid,
  created_at timestamptz not null default now(),
  primary key (device_id, client_op_id)
);
alter table devices enable row level security;
alter table device.ops enable row level security;   -- no policies: unreachable through the Data API
create policy "devices select" on devices for select using (household_id = my_household_id());
create policy "devices update" on devices for update using (household_id = my_household_id()) with check (household_id = my_household_id());
```

Functions. Every one is `security definer`, `set search_path = public, device`, and begins by revoking default access: `revoke execute on function … from public, anon, authenticated;` followed by the single intended grant.

- `pair_device(name text) returns text` — `authenticated` only. Random 32 bytes, hash stored, base64 returned once.
- `device.auth() returns devices` — private. Reads `x-device-key` from `request.headers`, hashes, matches an unrevoked device, raises `insufficient_privilege` otherwise. Called first in every device RPC; every subsequent lookup is constrained to `d.household_id`.
- `device_snapshot(protocol int) returns jsonb` — `anon`, POST, volatile (it updates `last_seen_at`). Fields:

  ```
  protocol, snapshot_at,
  source_status, source_since, source_observed_at, source_error, source_bounce, source_music,
  bed_min, rise_min, tz,
  settled_pct, settled_asleep_min, settled_awake_min, settled_wakings,   -- lamp: "crib settled", from observations since bed_min; null until 45 min of night
  bf: {id, side, elapsed_s, left_s, right_s, open_since, updated_at} | null,
  pump: {id, open_since, updated_at} | null,
  sleep: {id, open_since, source, timing_locked, updated_at} | null,
  last_feed: {at, kind, ml} | null, last_diaper: {at, kind} | null, last_pump: {at, ml} | null,
  today: {feeds, diapers, naps, nap_min, sleep_min},
  bottle_default_ml, caregivers: [{id, name}], boot_mode
  ```

  `updated_at` values are returned as the exact text PostgREST would give and echoed back verbatim.
- `device_sleep_today() returns jsonb` — `anon`, POST. Today's sleep rows (start, end, kind, source, place, duration) for the Log list and the six Stats cells (§7.3).
- `device_op(envelope jsonb) returns jsonb` — `anon`, POST. Envelope:

  ```
  { protocol, client_op_id, op, occurred_at, clock_ok, caregiver_id,
    target_id?, expected_updated_at?, args }
  ```

  Behaviour, in one transaction: authenticate; look up `(device_id, client_op_id)`; if present and `request_hash` matches, return the stored result; if present with a different hash, return `{"outcome":"rejected","reason":"op_id_reuse"}`; otherwise validate that `caregiver_id` and `target_id` belong to the device's household, apply, and store the result. Outcomes: `applied`, `duplicate`, `conflict` (with the current row), `invalid`, `unauthorized`, `retry`. Ops:

  | op | args | effect |
  | --- | --- | --- |
  | `bf_start` | side | insert breastfeed, one open segment, `started_at = occurred_at` |
  | `bf_switch` | — | close the open segment at `occurred_at`, open the other side; needs `expected_updated_at` |
  | `bf_stop` | — | close, set `left_s/right_s/end_side/ended_at`; needs `expected_updated_at` |
  | `bottle` | ml, kind | insert bottle at `occurred_at` |
  | `diaper` | wet, dirty | insert diaper at `occurred_at` |
  | `pump_start` / `pump_stop` | — / total_ml? | as breastfeed, `total_ml` when given |
  | `sleep_start` | place | insert manual sleep; `conflict` with the running row if one exists |
  | `sleep_stop` | — | set `ended_at`, `timing_locked = true` |
  | `sleep_dismiss` | — | soft-delete an auto sleep; `source_key` stays so it is never re-derived |
  | `delete` | — | soft-delete an entry this device created, versioned; used by the feed confirmation's Delete |

  Segment arithmetic is a port of `runningElapsed` and the stop path in `TimerBanner.svelte`; `summary.test.ts` remains the reference. Later, the web app can call the same functions so the two implementations cannot drift; that refactor is not required for the first release.

## 4. Poller — Edge Function `cradlewise-poll`

Secrets: `CW_TOKEN`, `HOUSEHOLD_ID`, `SCHEDULER_SECRET`. Scheduled by `pg_cron` every 30 s via `pg_net`, sending `x-scheduler-secret`; the function rejects any call without it before touching the network. The schedule is created with a fixed job name and `cron.unschedule` first, so re-running setup cannot double it.

Per tick:

1. **Lease**: `update sleep_status set lease_until = now() + interval '25 seconds' where household_id = $1 and (lease_until is null or lease_until < now()) returning *`. No row returned → exit. First run bootstraps the row.
2. **Admission**: in one statement, count `cw_requests` in the last 60 s (limit 2 for status) and the last 24 h (limit 2,880 total, with 200 reserved for reconciliation); if over, exit. Also exit if the status is settled sleeping or away and the last observation is under 60 s old. Insert the ledger row before the request.
3. **Fetch** `/baby/status`. `401` → `source_error = token_expired`. `403` → `forbidden` (subscription). `429` → `rate_limited`, respect `Retry-After` via `lease_until`. 5xx, timeout, bad JSON → `upstream` or `transport`, backoff doubling to 5 min in `lease_until`. Success clears `source_error`.
4. **Record** the observation; update `sleep_status` only if `observed_at` is newer than the stored one.
5. **Day metrics** at most every 30 min and only when the 24 h ledger has headroom; parse bed and rise minutes.
6. **Derive** (if `derive_enabled`) from the last 24 h of observations and the household's open sleep entry (§3.4 below, called 4.1 here).
7. **Reconcile** hourly and once after the household's rise time: fetch `/sleep/c-chart` for the last 36 h, spending from the reserve; adjust unlocked provisional rows, insert missed sleeps, clear `provisional`.

### 4.1 Derivation rules

A pure module, `derive-sleep.ts`, with no Deno imports, tested from the repo root (vitest `include` gains `supabase/functions/**/*.test.ts`).

- **Non-sleep** is one episode whether the labels are awake, crying or away; the 5 min debounce runs on the episode, not on each label.
- **Open** a provisional sleep when `sleeping` has been observed continuously for 5 min: `started_at` = that run's first `since`, `source_key = 'cw:' + since` (UTC, seconds), `provisional = true`, `place = 'crib'`, `created_by = null`, `via = 'cradlewise'`. Insert `on conflict (household_id, source_key) do nothing`. **Never open while any sleep is running** (§4.2).
- **Continue** through `stirring`.
- **Close** when a non-sleep episode reaches 5 min; `ended_at` = the episode's first `since`. `away` closes immediately once the sleep is 20 min old.
- **Gaps**: if consecutive observations are more than 4 min apart while a sleep is open, close it at the last observation with `uncertain_end = true`. A later `sleeping` with a new `since` opens a new row. Reconciliation repairs the join if Cradlewise says it was one sleep.
- **Kind and night_key**: `night` when `started_at` falls in `[bed_min − 60, rise_min)` local time (household timezone); `night_key` is the evening's date; rows after midnight inherit the previous evening's key.
- **Updates** are conditional: `update … where id = $1 and updated_at = $2 and coalesce((payload->>'timing_locked')::bool,false) = false`. A row that moved underneath is re-read on the next tick.

### 4.2 Manual timers and auto detection

- A manual sleep running with `place = 'crib'` and no `source_key` when a crib sleep becomes eligible is **adopted**: the deriver sets `source_key` on it and leaves its times alone. One row, no double counting.
- A manual sleep running with another place suppresses opening; the crib episode's `source_key` is recorded on the manual row's payload as `suppressed_source_key` so reconciliation does not re-insert it.
- **End now** on an auto row (banner, sheet, pad) sets `ended_at` and `timing_locked`; the poller then treats the episode as consumed and waits for a new `since` before opening again.
- **Not a nap** soft-deletes; the unique key survives; Restore is refused with an explanation while another sleep is running.
- A note or place edit leaves `timing_locked` unset, and the poller keeps closing the row normally.

Tests are transition sequences in, entry operations out: a clean nap; two stirrings; a false start (asleep 3 min then crying); awake 2 min then crying 3 min as one episode; pickup at 25 min; a night producing three rows with one `night_key`; an observation gap mid-sleep; reboot with an open row; note edit during a run; manual End while still sleeping; manual crib timer then eligibility; manual stroller timer then eligibility; dismiss then reconciliation; a concurrent human edit beating the conditional update.

## 5. Web app — adding sleep

Files in dependency order; everything reuses the sheet, timer and conflict machinery breastfeed already has.

### 5.1 Data layer

- `types.ts` — `'sleep'` in `EntryType`; `SleepPayload`, `SleepKind`; `total_ml` on `PumpPayload`; `source_key`, `via` on `Entry`; `SleepStatus`; `isSleep()`.
- `app.css` — `--sleep` (a lavender in the pastel family) and `--sleep-badge`.
- `Icon.svelte` — a `sleep` kind.
- `derive.ts` — `entryLabel` ("Nap · crib", "Night sleep", suffixes "· auto", "· Cradlewise, adjusted" when `timing_locked`, "· provisional" while unreconciled); `entryMagnitude` and `headline` as durations; `iconFor`; `sleepElapsedS(e, now)`; pump labels honour `total_ml`.
- `summary.ts`
  - `Summary.sleep = { naps, nap_s, longest_nap_s, night_s, sleep_s, uncertain: boolean }` where every `_s` is **interval overlap** with the window, half-open, clamped to the same `clock` the breastfeed branch uses. Counts (`naps`) still use the start instant so a nap is counted once.
  - New `nightSummary(entries, nightKey)` → `{ from, to, asleep_s, wakings, rows }`, wakings = rows − 1 within one `night_key`, never across calendar days.
  - `ofCard` gains `'sleep'`.
  - `windowFor` gains `'last-night'` using `night_key` of the most recent night.
- `store.svelte.ts` — `sleepStatus` state, loaded in `loadHousehold`, subscribed on the existing channel, **refreshed on `visibilitychange` and on realtime reconnect, cleared on sign-out**; running-entry queries (`refreshEntries` out-of-window fetch, replace/remove logic) include `sleep`; `pairDevice`, `listDevices`, `revokeDevice`, `setBootMode`.
- `validate.ts` — `sleepTooLong` above 16 h.

### 5.2 UI

- `ui.svelte.ts`, `SheetHost.svelte` — `SheetKind` gains `'sleep'`.
- **Every type dispatcher** gets an explicit `sleep` branch and a safe unknown fallback (open nothing, toast "Update the app to edit this entry"): `Card.svelte`'s `editSheet`, `history/+page.svelte`'s `editSheet`, `LogRow`, export, Recently deleted. A fixture with one row of each type, including an unknown type, drives a test over each mapper.
- `SleepSheet.svelte`
  - **Edit**: Kind (Nap / Night), Place chips, start and end `TimeRow`s, "still sleeping" toggle, `NoteRow`, Delete. Changing a time or ending sets `timing_locked`; changing note or place does not. The header shows "Cradlewise · provisional", "Cradlewise · adjusted" or "Manual".
  - **Timer**: Start inserts a running manual sleep; refusal from the unique index is explained with a link to the running one.
  - **Completed entry**: the same sheet opened from the card's + with "Log an earlier nap" pre-fills a closed interval; no need to start and stop a timer.
  - Dirty snapshot, version, `ConflictError`, discard confirmation as the other sheets. Saves spread the existing payload so unknown keys and provenance survive (also applied to Bottle, Diaper and Pump sheets, which currently rebuild their payloads).
- `PumpSheet.svelte` — **timer mode** (Start / Stop with live elapsed, Stop uses the real elapsed time, not the 20 min default) and a "total" amount field mapped to `total_ml`; opening a running pump shows the timer, not a duration form.
- `Card.svelte` — `'sleep'` card; `running` includes sleep; a crib chip in the band (dot, status, since, and **"crib update 12 min old"** in amber past 3 min, red with the error name past 15 min or on `source_error`).
- `+page.svelte` — fourth Card.
- `TimerBanner.svelte` — a stack: breastfeed, pump, sleep. Sleep reads "Napping · crib · auto"; its button is **End** for auto and **Stop** for manual; both set `timing_locked`. The pump banner is what makes finding 10 go away.
- `history/+page.svelte` — sleep rows; day line adds naps and nap time by overlap; a **Last night** strip at the top of each day group when a `night_key` exists.
- `SummaryView.svelte` — Sleep row (naps, nap time, longest, night sleep, wakings), plus a "Last night" segment beside Today / 24h.
- `settings/+page.svelte`
  - **Sleep monitor**: status and since, observation age, `source_error` in words (expired token → the `supabase secrets set CW_TOKEN=…` hint), requests in the last 24 h from the ledger, a derive on/off switch that writes `derive_enabled`.
  - **Devices**: list with last seen and boot mode; Pair shows the key once; Revoke.
  - Export gains `kind`, `source`, `place`, `via`, `total_ml`.

### 5.3 Tests and harness

- `summary.test.ts`: overlap across midnight (20:30–07:00 gives 7 h to the next day's Today), a rolling 24 h boundary, a DST night, a running nap clamped to the clock, longest nap, `nightSummary` with three rows and one key, no phantom wakings across two nights.
- `derive.test.ts`: labels, magnitudes, pump `total_ml`.
- `derive-sleep.test.ts`: the cases in §4.2.
- The Playwright harness moves from the scratchpad into `tests/e2e/` with its fixture and mock REST, and CI runs it headless. New checks: Sleep card chip and staleness, auto banner with End, sheet toggles, unknown-type fallback, pump timer Stop elapsed.

### 5.4 Out of scope on the web side

Nara import (the export has no sleep rows), sleep charts, Web Push on cries, a phone offline write queue (still a separate deliverable; the device queue does not supply one).

## 6. Rollout order for sleep

1. Apply 0004, 0005, 0006. Nothing reads or writes sleep yet.
2. Deploy the poller with `derive_enabled = false`. Observations and status accumulate; Settings shows source health. Reflash or unplug the standalone CradleWatch units first so the token has one client.
3. Deploy the web app with sleep support. Old tabs get the new bundle on next open; until then an old client cannot open a sleep row in the wrong editor because none exist.
4. Flip `derive_enabled`. Compare one full night and several naps against the Cradlewise app; document expected differences (debounce, gap handling).
5. Rollback is the flag; observations and human corrections stay.

## 7. Firmware — NurseryPad (fork of `CradleWatch/`)

Toolchain pinned: arduino-esp32 core version, M5Unified version, ArduinoJson version, in a `arduino-cli` compile job in CI. `secrets.h` fields: `WIFI_SSID`, `WIFI_PASS`, `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `DEVICE_KEY`, `TZ_STRING`.

### 7.1 Carried over unchanged

`BabyStatus`/`DisplayState` and the settled split, palette, songs and volume, haptics, night mode and the 15 s dim, the lamp renderer, the `Box` hit-test pattern, the throttled draw loop, the bundled-CA try-each loop.

### 7.2 Changes

- **Transport on its own task**: `netTask` on core 0 owns the `WiFiClientSecure`, runs the snapshot poll (15 s on hub and forms, 30 s in dashboard during settled sleep or away) and flushes the op queue; results go to the UI task through a FreeRTOS queue. Touch, timers, mute and Back never wait on a socket. Bundle **GTS Root R4** beside ISRG Root X1 and verify the chain from the board.
- **JSON**: ArduinoJson with a fixed-capacity document; every field read through typed accessors; nulls stay null; `protocol` mismatch shows an "update firmware" banner instead of guessing.
- **Freshness**: `source_observed_at` drives a "crib data 12 min old" banner on every screen including dashboard, lamp and the dimmed night screen; past 15 min the crib circle turns hollow grey and the lamp goes neutral.
- **Time**: NTP with the TZ string; elapsed timers anchor on `snapshot_at` and tick locally; `occurred_at` on ops comes from the synced clock, with `clock_ok = false` if NTP has not synced since boot.
- **Screens**: `enum Screen { HUB, FEED, FEED_TIMER, FEED_DONE, BOTTLE, CHANGE, PUMP, PUMP_TIMER, PUMP_AMOUNT, SLEEP_LOG, SLEEP_STATS, DASHBOARD }`. Idle 45 s (120 s mid-form, draft preserved) returns to HUB; DASHBOARD never times out; boot screen per `boot_mode`. Long-press 600 ms in DASHBOARD toggles lamp. Touch precedence: wake a dimmed screen → silence a crying alert → long press → navigation, fired on release. Buttons: A Back, B Home, C Dashboard, nothing else.
- **Chrome**: 36 px top bar (Back, title, clock, caregiver chip, crib circle); 36 px timer strip when `bf`, `pump` or `sleep` is set and the screen is not that timer's own; dashboard's speaker icon moves top-left.
- **Writes**: one-shot ops (bottle, diaper) are persisted to Preferences with their `occurred_at` before the toast says "saved locally", flushed in order, removed only on `applied` or `duplicate`; the queue holds 8 and refuses a ninth with a visible message. Timer ops require the network; the buttons grey out with "offline" when the last snapshot is older than 45 s. `conflict` shows a dialog naming what changed and offers refresh; nothing is silently dropped. States are legible: pending (hollow dot), saved (tick, 2 s), failed (red, with retry), stale (banner), unknown (grey circle).
- **Feed flow**: Stop commits (`bf_stop`); `FEED_DONE` is a confirmation showing the totals with **Delete** (versioned `delete` op) and **Done**. No Save button exists because nothing is unsaved.
- **Caregiver chip**: choices from the snapshot, selection in Preferences, sent as `caregiver_id`. The device name goes in `via`; the chip is household attribution, not authentication.

### 7.3 Screens and their data

Hub (snapshot); Feed and timers (snapshot `bf`); Bottle (`bottle_default_ml`); Change; Pump; **Sleep Log**: the auto card from `snapshot.sleep`, Start Nap, today's list from `device_sleep_today`; **Sleep Stats**, today only: total sleep (overlap with today), naps, longest nap, last night's sleep (`night_key` = yesterday evening), last night's wakings, awake in bed (Cradlewise day metric), with a footer of rise and bed times and the observation age. Seven-day averages stay on the phone. Dashboard as CradleWatch, plus bounce and music indicators from the snapshot.

## 8. Releases and gates

| Release | Scope | Gate (from `ACCEPTANCE-CHECKLIST.md`) |
| --- | --- | --- |
| **A. Contracts and fixtures** | This document; captured, redacted payloads from `/baby/status`, `/sleep/day-metrics`, `/sleep/c-chart` with a live token; harness committed; `.gitignore` fixed | Every finding has a disposition (done above); fixtures exist |
| **B. Backend collection** | 0004–0006; poller with lease, ledger, scheduler secret, derivation off; grants and RLS; deriver tests | DB-01, DB-02, SEC-01–03, POLL-01–04 on a disposable database; source health visible in Settings |
| **C. Sleep on the phones** | §5 in full; pump timer parity; deploy; then `derive_enabled` on | SLEEP-01–08, SUM-01–03, WEB-01–04, REL-01–02; one night and several naps compared with the Cradlewise app |
| **D. Read-only NurseryPad** | Snapshot RPC, fork with new transport and parser, hub, dashboard, lamp, freshness, Sleep Stats, revocation | FRESH-01, DEV-01–03, SEC-04, HW-01 on two units |
| **E. One-shot device logs** | `device_op` for bottle and diaper, durable queue, undo/cancel semantics | OP-01–05 |
| **F. Shared timers** | Feed, pump, sleep timer ops with conflict UI; confirmation-with-delete flow | TIMER-01 |
| **G. Offline timer chains** | Dependent replay with local session ids | TIMER-02–03 |

Rough sizes, for planning only: B one session, C two, D one to two, E one, F one, G one. The gates decide readiness, not the sizes.

Deferred explicitly: Supabase Realtime over WebSocket on the device, seven-day device charts, c-chart-driven night score beyond the settled percentage, temperature units, Growth charts, Siri, a phone offline queue.

## 9. Open items to verify at the start of release A

- Sub-minute `pg_cron` on the project; otherwise a 60 s cron with two polls per invocation.
- The c-chart payload shape and whether its session identities are stable across queries; the deriver's reconciliation tests are built on captured payloads, not on assumptions.
- The certificate chain Supabase presents to the ESP32.
- Whether Cradlewise's rolling limits are enforced per token or per account, which decides how strict the migration cutover must be.
