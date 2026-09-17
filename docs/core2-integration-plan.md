# Core2 Nursery Pad and Sleep — implementation plan

Written 2026-09-17 against Babytracker `main` (94edc8c) and the CradleWatch sketch (M5Stack Core2, M5Unified, Cradlewise Data API). Design mockup: the "Core2 Nursery Pad" canvas (tap-through prototype plus twelve static screens).

## 0. Decisions already made

| Topic | Decision |
| --- | --- |
| Where Cradlewise is polled | Supabase, by a scheduled Edge Function. The Core2 stops talking to Cradlewise. |
| Device transport | PostgREST RPC with the anon key plus a per-device secret header. No Edge Function on the device path. |
| Device resting screen | Home hub. Dashboard (CradleWatch) is a mode entered by the crib-state circle top-right, from any screen; tap anywhere in it returns to the hub. |
| Sleep in the data model | New `entry_type` value `sleep`. Running sleep = `ended_at IS NULL`, exactly like breastfeed and pump. No separate nap table. |
| Auto vs manual | Cradlewise-derived sleeps are ordinary `sleep` entries with `payload.source = 'cradlewise'`, keyed by `source_key` for idempotency. Manual naps have `source = 'manual'`. Stats never add Cradlewise minutes to logged minutes; crib naps are already entries. |
| Attribution | Auto rows have `created_by = null` and render as "Cradlewise". Device taps carry the caregiver chosen on the device and `payload.via = 'core2:<device>'`. |
| Timer semantics | The running entry in Postgres is the single source of truth. Phone and pad both read it, both can stop it, conditional on `updated_at` as today. |

Rejected alternative, kept for the record: the Core2 keeps polling Cradlewise and reports transitions upward. It preserves the current 30 s cry latency but makes nap logging depend on a powered device, doubles credentials on the ESP32, and makes a second monitor a budget problem (2,880 requests/day is one poller's worth). If the extra ~30 s on cry alerts turns out to matter in practice, this hybrid is the fallback: same schema, same web app, only the poller moves.

## 1. Architecture

```
Cradlewise Data API ──(30 s cron)──▶ Edge Function cradlewise-poll
                                        │  writes sleep_status (1 row/household)
                                        │  appends sleep_transitions
                                        │  derives sleep entries (nap / night)
                                        ▼
                                   Supabase Postgres ◀──── web app (supabase-js, RLS, realtime)
                                        ▲
                                        │  RPC device_snapshot / device_op
                                        │  apikey: anon, x-device-key: per device
                                   Core2 units (1..n)
```

Budget: Cradlewise allows 2 status requests/minute and 2,880/day. A 30 s cron is exactly 2,880/day, so the poller keeps CradleWatch's skip rule (poll every 60 s while settled asleep or away) and counts day-metrics and retries against the same daily counter. Device reads hit PostgREST, which is uncapped on the free tier; three devices at one snapshot per 15 s is ~260 MB/month of egress against a 5 GB allowance.

## 2. Database — migration `0004_sleep_and_devices.sql`

Apply with the Supabase MCP `apply_migration` as before; each block is safe on the live data.

### 2.1 Sleep entries

```sql
alter type entry_type add value 'sleep';

-- Idempotency key for derived rows: 'cw:<since ISO>'. Unique regardless of deleted_at, so a
-- soft-deleted ("Not a nap") row blocks re-derivation for good.
alter table entries add column source_key text;
create unique index entries_source_key_key on entries (source_key) where source_key is not null;

-- One running sleep per household, like breastfeed and pump.
create unique index entries_one_running_sleep on entries (household_id)
  where type = 'sleep' and ended_at is null and deleted_at is null;
```

Payload contract (`SleepPayload` in `types.ts`):

```ts
{ kind: 'nap' | 'night', source: 'cradlewise' | 'manual', edited: boolean,
  place?: 'crib' | 'stroller' | 'car' | 'arms' | 'other', via?: string }
```

`edited` flips to true the first time a human saves the row from the Sleep sheet. The deriver never updates a row with `edited = true`.

### 2.2 Cradlewise state

```sql
create table sleep_status (
  household_id uuid primary key references households on delete cascade,
  status text not null check (status in ('sleeping','awake','stirring','crying','away','unknown')),
  since timestamptz,                -- Cradlewise "since" for the current status
  observed_at timestamptz not null, -- last successful poll
  token_ok boolean not null default true,
  bed_min int, rise_min int,        -- learned from day-metrics, minutes after local midnight
  day_metrics jsonb,                -- last day-metrics payload, for Stats
  requests_today int not null default 0,
  requests_day date,
  retry_after timestamptz,
  updated_at timestamptz not null default now()
);
create table sleep_transitions (
  id bigserial primary key,
  household_id uuid not null references households on delete cascade,
  status text not null,
  since timestamptz not null,
  observed_at timestamptz not null
);
create index sleep_transitions_hh_since on sleep_transitions (household_id, since desc);
alter table sleep_status enable row level security;
alter table sleep_transitions enable row level security;
create policy "sleep_status select" on sleep_status for select using (household_id = my_household_id());
create policy "sleep_transitions select" on sleep_transitions for select using (household_id = my_household_id());
alter publication supabase_realtime add table sleep_status;
create trigger sleep_status_set_updated_at before update on sleep_status for each row execute function set_updated_at();
```

Writes come only from the poller (service role), so no insert/update policies.

### 2.3 Devices and device RPCs

```sql
create table devices (
  id uuid primary key default gen_random_uuid(),
  household_id uuid not null references households on delete cascade,
  name text not null,
  key_hash bytea not null,           -- sha256 of the plaintext key shown once at pairing
  last_seen_at timestamptz,
  created_at timestamptz not null default now()
);
create table device_ops (           -- idempotent op log; a retried client_id returns the stored result
  client_id uuid primary key,
  device_id uuid not null references devices on delete cascade,
  op text not null,
  result jsonb,
  created_at timestamptz not null default now()
);
alter table devices enable row level security;
create policy "devices select" on devices for select using (household_id = my_household_id());
create policy "devices delete" on devices for delete using (household_id = my_household_id());
```

Functions, all `security definer`, `set search_path = public`:

- `pair_device(name text) returns text` — for authenticated caregivers. Generates 32 random bytes, stores the hash, returns the base64 plaintext once. Settings calls it.
- `device_auth() returns devices` (private) — reads `current_setting('request.headers', true)::json ->> 'x-device-key'`, hashes it, returns the matching device or raises `insufficient_privilege`. Updates `last_seen_at`.
- `device_snapshot() returns jsonb` — grant execute to `anon`. One flat object, keys fixed so the ESP32 string pickers work without a JSON library:

  ```
  server_now, crib_status, crib_since, crib_token_ok, bed_min, rise_min,
  night_pct, night_asleep_min, night_awake_min, night_wakings,
  bf_id, bf_side, bf_elapsed_s, bf_left_s, bf_right_s, bf_open_since, bf_updated_at,
  pump_id, pump_open_since, pump_updated_at,
  sleep_id, sleep_open_since, sleep_auto, sleep_updated_at,
  last_feed_at, last_feed_kind (L|R|bottle), last_feed_ml, last_diaper_at, last_diaper_kind,
  last_pump_at, last_pump_ml, today_feeds, today_diapers, today_naps, today_nap_min,
  bottle_default_ml, cg1_id, cg1_name, cg2_id, cg2_name
  ```

  Night score is computed here from `sleep_transitions` since `bed_min`, with CradleWatch's rule (sleeping and stirring count as good), so the lamp page needs no device-side history.
- `device_op(client_id uuid, op text, args jsonb) returns jsonb` — grant execute to `anon`. Looks up `device_ops` first and returns the stored result on a repeat. Ops and their args:

  | op | args | effect |
  | --- | --- | --- |
  | `bf_start` | side, caregiver | insert breastfeed, `segments=[{side,start,end:null}]`, `manual=false` |
  | `bf_switch` | id, expected_updated_at | close the open segment, open the other side |
  | `bf_stop` | id, expected_updated_at | close the segment, set `left_s/right_s/end_side/ended_at` |
  | `bottle` | ml, kind, caregiver | insert bottle |
  | `diaper` | wet, dirty, caregiver | insert diaper |
  | `pump_start` / `pump_stop` | caregiver / id, expected_updated_at, ml? | as breastfeed; `ml` splits nowhere, so it lands in `left_ml` with `right_ml` null and the sheet shows "total" |
  | `sleep_start` | caregiver, place | insert manual sleep; refused with `sleep_running` if one is open |
  | `sleep_stop` | id, expected_updated_at | set `ended_at` |
  | `sleep_dismiss` | id | soft-delete an auto sleep ("Not a nap") |
  | `undo` | target client_id | soft-delete the entry that op created, within 30 s |

  Conflicts return `{"error":"conflict"}` and the device refetches the snapshot; the segment arithmetic is a port of `runningElapsed` and the stop path in `TimerBanner.svelte`, and `summary.test.ts` remains the reference for what the totals must be.

Revoke `execute` from `anon` and `authenticated` on every other public function that is not meant to be called directly (Supabase grants execute to both by default).

## 3. Edge Function `cradlewise-poll`

Deno, under `supabase/functions/cradlewise-poll/`. Secrets: `CW_TOKEN`, `HOUSEHOLD_ID`, `TZ_STRING`, plus the auto-provided service role. Scheduled from Postgres:

```sql
select cron.schedule('cradlewise-poll', '30 seconds',
  $$ select net.http_post(url := '<functions-url>/cradlewise-poll',
       headers := '{"Authorization":"Bearer <anon key>"}'::jsonb) $$);
```

Per tick:

1. Load `sleep_status`. If `retry_after` is in the future, or the status is settled sleeping/away and the last poll was under 60 s ago, return.
2. GET `/baby/status` with the bearer token. `401` → `token_ok=false`, return. `429` → set `retry_after` from the header. Transport error → exponential backoff stored in `retry_after`, capped at 5 min. Increment `requests_today`, resetting on a new local date.
3. Write `sleep_status`. If `status` or `since` changed, append to `sleep_transitions`.
4. Every 30 minutes, GET `/sleep/day-metrics` for the local date; store `day_metrics`, parse bed and rise into minutes.
5. Run the deriver (below) against the last 24 h of transitions and the household's open sleep entry.

The deriver is a pure module, `derive-sleep.ts`, with no Deno imports so vitest can run it from the repo root:

- **Open** a sleep when status has been `sleeping` continuously for 5 minutes. `started_at` is that transition's `since`. `source_key = 'cw:' + since`. Insert with `on conflict (source_key) do nothing`, `created_by = null`, `payload = {kind, source:'cradlewise', edited:false, place:'crib'}`.
- **Continue** through `stirring`. Stirring never starts a sleep and never ends one.
- **Close** when `awake`, `crying`, or `away` has persisted 5 minutes; `ended_at` is the first non-sleep transition's `since`. `away` closes immediately once the sleep is 20 minutes old (a pickup at the end of a nap, not a blip).
- **Kind** is `night` when `started_at` falls in `[bed_min - 60, rise_min)` local time, else `nap`. Defaults 20:30 and 07:00 until day-metrics say otherwise, the same defaults as CradleWatch.
- **Never** update a row with `payload.edited = true`; never re-insert a `source_key` that exists, deleted or not.

Tests for the deriver are transition sequences in, entry operations out: a clean nap, a nap with two stirrings, a false start (asleep 3 min then crying), a pickup mid-nap, a night with two wakings producing three `night` rows, a mid-run reboot (open entry already exists), a human edit that must be left alone.

## 4. Web app — adding sleep

Files in dependency order. Everything reuses the sheet, timer and conflict machinery that already exists for breastfeed.

### 4.1 Data layer

- `src/lib/data/types.ts` — add `'sleep'` to `EntryType`; add `SleepPayload` and `SleepKind`; add it to the `Payload` union; `isSleep()`. Add `source_key: string | null` to `Entry`.
- `src/app.css` — a `--sleep` token in the existing pastel family (a lavender, e.g. `#cfc8f2`) and `--sleep-badge`.
- `src/lib/ui/Icon.svelte` — a `sleep` kind (moon over the same tinted ellipse the others use).
- `src/lib/data/derive.ts`
  - `entryLabel`: "Nap · crib" / "Nap · stroller" / "Night sleep", with " · auto" when `source === 'cradlewise' && !edited`.
  - `entryMagnitude`: duration in seconds, so History bars scale naps against the longest nap.
  - `headline`: the duration; for a running sleep the elapsed so far.
  - `iconFor`: `'sleep'`.
  - new `sleepElapsedS(e, now)` for running sleeps (no segments; `started_at` to `min(now, ended_at)`).
- `src/lib/data/summary.ts`
  - `Summary.sleep = { naps, nap_s, longest_nap_s, night_s, wakings, running: boolean }`.
  - `case 'sleep'` in `summarize`, counting a running sleep up to the same `clock` the breastfeed branch uses, so the History clock rule from audit 2 holds.
  - `wakings` = number of `night` rows in the window minus one, floored at zero. Each waking splits the night into a new entry, so no transition data is needed client-side.
  - A night sleep spanning midnight belongs to the day it started, consistent with `inWindow` using `started_at`. Note this in the History day header ("night 9h 40m" appears on the evening it began).
  - `ofCard` gains `'sleep'`.
- `src/lib/data/store.svelte.ts`
  - `sleepStatus = $state<SleepStatus | null>(null)`; load it in `loadHousehold`; subscribe to `sleep_status` on the existing realtime channel (a second `postgres_changes` filter on the same channel, so the live/offline indicator stays one thing).
  - `insert` already sets `child_id` for everything but pump; sleep gets the child.
  - `pairDevice(name)` and `listDevices()` / `removeDevice(id)` wrappers for Settings.
- `src/lib/data/validate.ts` — `sleepTooLong(start, end)` warning above 16 h; end-before-start is already a DB check and a `TimeRow` validation.

### 4.2 UI

- `src/lib/data/ui.svelte.ts` and `src/lib/components/SheetHost.svelte` — `SheetKind` gains `'sleep'`; host maps it to `SleepSheet`.
- `src/lib/components/sheets/SleepSheet.svelte` — modelled on `BreastfeedSheet` minus sides:
  - **Edit mode**: Kind segmented control (Nap / Night), Place chips (crib, stroller, car, arms), start `TimeRow`, end `TimeRow` with a "still sleeping" toggle that nulls `ended_at`, `NoteRow`, Delete in the footer. Saving a Cradlewise row sets `edited = true` and keeps `source_key`.
  - **Timer mode** (opened from the card's + with nothing running): Start inserts `ended_at: null`, `source: 'manual'`; the sheet then shows the live elapsed and Stop. Starting while an auto sleep is open is refused by the unique index; the sheet explains "Crib nap already running" and offers to open it instead.
  - Dirty snapshot, `version` / `ConflictError` handling and the discard confirmation exactly as the other sheets.
- `src/lib/components/Card.svelte` — extend the `card` union with `'sleep'`; the `running` derivation includes `type === 'sleep'`; when `store.sleepStatus` exists the band shows a crib chip: coloured dot, status word, "since 20:51". The chip is informational; the + opens the running sleep if any, else the timer sheet.
- `src/routes/+page.svelte` — a fourth `Card`: `card="sleep" title="Sleep" color="var(--sleep)" lastLabel="Last sleep" sheetFor="sleep"`.
- `src/lib/components/TimerBanner.svelte` — generalise from "the running breastfeed" to a list of running timers (breastfeed, sleep), rendered as stacked banners inside the layout's `.top-stack`. The sleep banner reads "Napping · crib · auto" or "Napping · stroller" with a live clock; its button says **End** for an auto sleep and **Stop** for a manual one, both writing `ended_at` conditionally. Suppressed while the corresponding sheet is open, as now.
- `src/routes/history/+page.svelte` — `editSheet` maps sleep; the day stats line adds "N naps · Xh Ym" and "night Xh Ym" when present. Rows already flow through `LogRow`; add the auto marker there via `entryLabel`.
- `src/lib/components/LogRow.svelte` — `store.initial(created_by)` returns "C" for null rows with `source === 'cradlewise'` (extend `initial` to take the entry), and the device suffix in the `title` when `via` is set.
- `src/lib/components/SummaryView.svelte` — a Sleep row: naps badge, nap total, longest nap, night sleep, wakings. Today/24h windows apply unchanged.
- `src/routes/settings/+page.svelte`
  - **Sleep monitor** section: crib status and since, last poll age, token state (with the "regenerate on the Cradlewise dashboard, then `supabase secrets set CW_TOKEN=…`" hint when expired), requests today.
  - **Devices** section: list with last seen; "Pair a device" prompts for a name, calls `pairDevice`, shows the key once with a copy button; Remove per row.
  - Export: the CSV column mapping gains `kind`, `source`, `place`; JSON needs nothing.
  - Recently deleted already covers sleep; "Not a nap" dismissals appear there and Restore works because `source_key` survives.

### 4.3 Tests and harness

- `summary.test.ts`: nap count and totals, a running nap counted to the clock and not past `to`, a night across midnight attributed to its start day, wakings from three night rows, longest nap.
- `derive.test.ts` (new): labels, magnitude, elapsed for running sleep.
- `derive-sleep.test.ts` at the repo root config, importing the poller's pure module; cases listed in §3.
- Playwright harness (`scratchpad/pw`): fixture rows for `sleep` plus a mocked `sleep_status`; checks for the Sleep card chip, the auto banner with End, the sheet's "still sleeping" toggle, and the unique-index refusal path (mock a 409 on insert).
- `svelte-check` will flag every exhaustive `switch` on `EntryType`; that list is the checklist of places sleep must be handled.

### 4.4 Out of scope on the web side

Nara import (the export has no sleep rows: 347 breastfeed, 271 diaper, 119 bottle, 70 pump, 12 growth, 2 combo, and milestone/vaccine/profile rows already skipped), sleep charts, Web Push on cries.

## 5. Firmware — NurseryPad (fork of CradleWatch)

Same toolchain: arduino-esp32, board M5Core2, M5Unified only. New `secrets.h` fields: `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `DEVICE_KEY`, plus the existing Wi-Fi and `TZ_STRING`; `CW_TOKEN` goes away.

### 5.1 What carries over unchanged

`BabyStatus`/`DisplayState` and the settled-after-10-minutes split, the palette, all five songs and the volume cycle, haptics, night mode and the 15 s dim, the lamp page rendering, the stale-data and token banners, the `Box` hit-test pattern, the 5 ms loop with throttled draws, the tiny `jsonStr`/`jsonNum` pickers, the bundled-CA try-each loop.

### 5.2 What changes

- **Transport**: `api.h` replaces the Cradlewise client. Two calls: `GET /rest/v1/rpc/device_snapshot` (PostgREST accepts GET for zero-arg functions) and `POST /rest/v1/rpc/device_op`, both with `apikey`, `Authorization: Bearer <anon>` and `x-device-key`. Bundle **GTS Root R4** alongside ISRG Root X1 (Supabase sits behind Cloudflare); verify the chain with `openssl s_client -connect kmblhrpgvvscwctcqfhq.supabase.co:443 -showcerts` before flashing.
- **Time**: `configTime` with the TZ string and NTP so the top-bar clock is right; elapsed timers use `server_now` from the snapshot as the epoch and tick locally, so device clock drift cannot show in a timer.
- **Snapshot cadence**: every 15 s on the hub and forms, 30 s in dashboard mode during settled sleep or away (mirrors the old adaptive rule, now free). Crib-state changes between snapshots drive the same chime/haptic/override code that transitions drove before.
- **Screen state machine**: `enum Screen { HUB, FEED, FEED_TIMER, FEED_SUMMARY, BOTTLE, CHANGE, PUMP, PUMP_TIMER, PUMP_AMOUNT, SLEEP_LOG, SLEEP_STATS, DASHBOARD }` with one `draw*()` and one `touch*()` per screen, dispatched from the loop. Idle timeout 45 s (120 s mid-form) returns to HUB; DASHBOARD never times out. Long-press (600 ms) in DASHBOARD toggles the lamp page. The three capacitive buttons map to Back, Home, Dashboard.
- **Chrome**: 36 px top bar (Back, title, clock, caregiver chip, crib circle) on every screen but DASHBOARD; 36 px timer strip along the bottom whenever `bf_id`, `pump_id` or `sleep_id` is set and the current screen is not that timer's own. Dashboard's speaker icon moves to the top-left; top-right becomes the Home target.
- **Writes**: optimistic. A tap updates local state and draws immediately, enqueues `{client_id, op, args}` in an 8-slot ring mirrored to Preferences, and `flushQueue()` posts one op per loop pass when Wi-Fi is up. A `conflict` result drops the op and forces a snapshot. A red dot on the strip shows while the queue is non-empty. UNDO on the 5 s toast enqueues `undo` with the original `client_id`.
- **Caregiver chip**: two names and ids from the snapshot; the selection persists in Preferences and is sent with every op.
- **Night dimming**: derived from `bed_min`/`rise_min` in the snapshot and the crib status, applied to every screen.

### 5.3 Screens (see the canvas for layout)

Hub with four tiles and a status line; Feed (Left / Right / Bottle); breastfeed timer (Switch / Stop) and Save/Discard summary; Bottle amount ±10 ml with breast-milk/formula toggle; Change as three one-tap columns; Pump start, timer, amount with Skip; Sleep with Log (auto-nap card with End now / Not a nap, Start Nap, today's list) and Stats (2×3 cells for today, 7-day averages on the C button, Cradlewise rise/bed footer); Dashboard mode as CradleWatch.

## 6. Phases

| Phase | Deliverable | Acceptance | Size |
| --- | --- | --- | --- |
| **1. Backend** | Migration 0004; `cradlewise-poll` with the deriver and its tests; cron scheduled; `CW_TOKEN` in secrets | `sleep_status` updates every 30–60 s; a real nap appears as a `sleep` row within 5 min of falling asleep and closes within 5 min of waking; `requests_today` stays under 2,880 | 1 session |
| **2. Web app** | §4 in full, PR with CI green, harness checks passing, deployed to Netlify | Sleep card with crib chip; auto nap banner with End; manual nap timer; Sleep sheet edit incl. "Not a nap" via Delete; Summary and History sleep numbers match the fixture; Settings shows monitor health and pairs a device | 1–2 sessions |
| **3. Device read path** | Device RPCs (`device_snapshot`, `pair_device`), NurseryPad fork with new transport, hub + dashboard mode, timer strip read-only, Sleep Stats | Two units show the same state within one snapshot interval; chimes and overrides behave as CradleWatch did; lamp page score matches the web Summary | 1 session |
| **4. Device write path** | `device_op` with the op table and idempotency; Feed, Change, Pump, Sleep Log screens; queue, toast/undo, caregiver chip | A feed started on the pad shows on both phones within a second and can be stopped from either; a diaper tapped during a Wi-Fi drop lands once when Wi-Fi returns; no duplicate on retry | 2 sessions |
| **5. Optional** | Supabase Realtime over WebSocket on the device; c-chart reconciliation of derived sleeps; night-score card in the web app; a temperature unit posting to the same hub | — | as wanted |

Phase 1 and 2 ship value on their own (nap logging in the app) before any firmware exists. Phase 3 must precede 4 because the write path assumes the snapshot for reconciliation.

## 7. Things to verify before Phase 1

- Sub-minute `pg_cron` schedules (`'30 seconds'`) are available on this project's Postgres; if not, run the cron at one minute and poll twice inside the function with a 30 s sleep.
- The exact JSON of `/baby/status`, `/sleep/day-metrics` and `/sleep/c-chart` from a live token; the sketch's pickers document the fields it used, but the deriver's tests should be built on captured payloads.
- `alter type … add value` cannot run inside a transaction block with other statements on some Postgres versions; apply it as its own migration step if `apply_migration` complains.
- The certificate chain Supabase presents to the ESP32 (GTS vs ISRG) as noted above.

## 8. Risks

- **Token expiry** now silently stops nap logging. `token_ok=false` shows in Settings and as a banner on the device; rotation is a secret update, not a reflash. Consider a Settings banner in the web app as well so it is seen daily.
- **Cry latency** grows by up to one cron tick plus one snapshot interval. Phase 5's WebSocket removes the second term.
- **Mis-attribution** from a stale caregiver chip on the pad. The chip is on every screen and on the Save summary for this reason.
- **Free-tier project pausing** after inactivity would stop the cron; daily app use prevents it, and the poller's own requests count as activity.
- **Anon-callable RPCs** are gated only by the device key. Keys are 256-bit random, hashed at rest, revocable from Settings; `device_ops` doubles as an audit log.
