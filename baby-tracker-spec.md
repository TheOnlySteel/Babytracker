# Baby Tracker — Design Spec

Replacement for the Nara Baby app, scoped to feed, diaper and pump logging for two caregivers. Written 2026-09-16 from the Nara CSV export (`export_narababy_rosalie_20260916.csv`) and eleven reference screenshots (IMG_2593–IMG_2604). Reference screenshots define the target look; the export defines the data model and is the test fixture.

## 1. Goal and scope

Two caregivers (Steel, Dominique) log Rosalie's feeds, diapers and pumping from their iPhones, see the same log within a second of each other, and get a "what happened today / last 24h" summary. Must import the full Nara history so nothing is lost at cutover.

**In scope (v1):** bottle feed, diaper, breastfeed with live timer, pump, Activity screen, Summary sheet, day-by-day History, Nara CSV import, two-caregiver realtime sync, installable PWA.

**Out of scope:** sleep, milestones, vaccines, mom/postpartum tracking, photos, multiple children, guides. Growth (WHO percentile charts) is a phase-4 nice-to-have, not v1.

**Non-goals:** a native app, App Store distribution, iOS Live Activities / Dynamic Island. Accept that a PWA cannot show a running timer on the lock screen; mitigate per §6.

## 2. Stack

- **Backend:** Supabase — Postgres, Auth (magic link or Apple sign-in), Realtime on `entries`, RLS scoped to `household_id`. Optional Edge Function for Siri Shortcut endpoints.
- **Frontend:** small SPA installed to the home screen. SvelteKit (preferred) or Vite + Preact. Not Astro — this is all client state.
- **Hosting:** Netlify. PWA manifest, service worker for shell caching. Offline write queue is phase 3; home Wi-Fi is the assumed environment.
- **Icons:** Lucide or Phosphor. Do not reproduce Nara's illustrations.
- **Fonts:** Fraunces (headings, big numerals) and Inter (everything else) via Google Fonts, with system fallbacks. Playfair Display is an acceptable substitute for Fraunces.

## 3. Data model

One `entries` table with a type discriminator and a `jsonb` payload. This mirrors the Nara export exactly and keeps the import trivial.

```sql
create table households (id uuid primary key default gen_random_uuid(), name text);
create table children (
  id uuid primary key default gen_random_uuid(),
  household_id uuid references households not null,
  name text not null, sex text, birth_date date not null
);
create table caregivers (
  user_id uuid primary key references auth.users,
  household_id uuid references households not null,
  display_name text not null            -- 'Steel', 'Dominique'
);

create type entry_type as enum ('breastfeed','bottle','combo','diaper','pump','growth');

create table entries (
  id uuid primary key default gen_random_uuid(),
  household_id uuid references households not null,
  child_id uuid references children,     -- null for pump (it's the mother's, not the child's)
  type entry_type not null,
  started_at timestamptz not null,
  ended_at timestamptz,                  -- null = timer still running (breastfeed, pump)
  payload jsonb not null default '{}',
  note text,
  created_by uuid references caregivers,
  updated_by uuid references caregivers,
  created_at timestamptz default now(),
  updated_at timestamptz default now(),
  nara_activity_key text unique          -- idempotent re-import
);
create index on entries (household_id, started_at desc);
create index on entries (household_id, type, started_at desc);
```

RLS: every table filtered by `household_id = (select household_id from caregivers where user_id = auth.uid())`. Store SI units (mL, cm, kg, seconds); convert at display.

### Payload shapes

| type | payload |
|---|---|
| `bottle` | `{ kinds: ['breast_milk' \| 'formula'], breast_milk_ml, formula_ml, formula_brand }` — `kinds` is a set; export type "Breast Milk Formula" means both. `formula_ml`/`breast_milk_ml` split when both; a plain `volume_ml` is derived. |
| `breastfeed` | `{ begin_side, end_side, left_s, right_s, manual: bool, segments: [{side, start, end}] }` — `segments` is the timer's raw record; `left_s`/`right_s` are the derived totals and the only thing the import can fill. `manual` = durations typed, not timed (Nara's `.nonTimer` suffix). |
| `combo` | breastfeed fields + bottle fields. Exists in export (2 rows); support on import and display, no dedicated entry sheet in v1. |
| `diaper` | `{ wet, dirty, dry: bool, texture: text[], color: text[], blowout, rash: bool }` — `wet`+`dirty` both true is the modal case. `texture` ∈ runny, mucousy, mushy, solid, pebbles. `color` ∈ black, green, yellow, brown, red, gray. Both are sets (export shows `BROWN YELLOW`, `MUSH RUN`). |
| `pump` | `{ left_ml, right_ml }` — total derived. Duration = `ended_at − started_at`. |
| `growth` | `{ weight_kg, height_cm, head_cm }` — any subset. Phase 4. |

## 4. Nara CSV import

One script, run once at cutover and re-runnable (upsert on `nara_activity_key`). Column mapping:

| Nara column | → |
|---|---|
| `Type` | `type` (Breastfeed→breastfeed, Bottle Feed→bottle, Combo Feed→combo, Diaper→diaper, Pump→pump, Growth→growth; skip Milestone, Vaccine, Baby First, Profile) |
| `Start Date/time (Epoch)` | `started_at` (epoch ms; `Time Zone` column is America/Vancouver, informational only) |
| `[Pump] End Date/time (Epoch)` | `ended_at` for pump; for breastfeed compute `started_at + left_s + right_s` |
| `Created By Caregiver` / `Last Updated By Caregiver` | map name → caregiver `user_id` |
| `Note` | `note` |
| `_activityKey` | `nara_activity_key` |
| `[Diaper] Type` | Wet→wet; Dirty→dirty; Dirty Wet→both; Dry→dry |
| `[Diaper] Detail` | Blowout→blowout, Rash→rash |
| `[Diaper] Dirty Color` / `Dirty Texture` | space-separated tokens → arrays, lowercased (RUN→runny, MUSH→mushy, PEBBLE→pebbles, MUCOUS→mucousy, SOLID→solid) |
| `[Bottle Feed] Type` | Formula / Breast Milk / Breast Milk Formula → `kinds` |
| `[Bottle Feed] Formula Name`, `Formula Volume`, `Breast Milk Volume`, `Volume` | brand and mL fields (units are all ML in this export; assert, don't assume) |
| `[Breastfeed] Begin Side`, `End Side`, `Left Duration`, `Right Duration` | sides (strip `.nonTimer` → `manual: true`), seconds |
| `[Pump] Left Volume`, `Right Volume` | mL |
| `[Growth] Weight` + unit, `Height` + unit, `Head Size` + unit | convert LB→kg, CM stays |

**Acceptance test:** after import, the Summary for 2026-09-16 (Today, computed at 12:30 PT) must show breastfeed 2 / 40m (20 left, 20 right); bottle 5 / 375 mL formula; diaper 7 (7 wet, 3 dirty); pump 1 / 130 mL (70 L, 60 R). Those numbers are in IMG_2597 and reconcile exactly against the export. Last-24h at the same moment: breastfeed 9 / 2h38m; bottle 8 / 715 mL (90 breast milk, 625 formula); diaper 13 (13 wet, 5 dirty); pump 1 / 130 mL (IMG_2596).

Export contents for sizing: 833 rows over 37 days — 347 breastfeed, 271 diaper, 119 bottle, 70 pump, 12 growth, 2 combo.

## 5. Screens

### 5.1 Activity (home) — IMG_2594, IMG_2598, IMG_2600
Header: child avatar, name in serif, today's date, summary button, overflow. Then one card per type in fixed order Feed, Diaper, Pump (Growth if phase 4).

Each card: coloured header band with the type name in serif; a blue circular **+** button overlapping the band's bottom-right edge; a "last" row — icon left, "Last feeding / 2h 29m ago" in the middle, headline value oversized in serif on the right (`45 mL`, `wet`, `130 mL`); a "Show More" disclosure that expands today's entries for that type inline.

Expanded log row (IMG_2594): icon, `10:00am  Formula`, then a horizontal bar whose length is proportional to volume (bottle) or duration (breastfeed) relative to the day's max, value label at bar end, chevron to edit. Yesterday's rows prefixed `YD`.

**Departure from Nara (do this):** each Feed card gets a one-tap **Same again** action (`45 mL formula`, or `Left, 20m` for a manual breastfeed) with a 4-second undo toast. The sheet is for exceptions.

### 5.2 Feed type picker — IMG_2601
Tapping Feed **+** opens a bottom sheet: Breastfeed, Bottle Feed, Combo Feed. v1 ships the first two; Combo can be hidden until built.

### 5.3 Bottle Feed sheet — IMG_2602, IMG_2603
Yellow title bar (× left, "Bottle Feed" centred serif, Save right). Two circle toggles: breast milk / formula, multi-select. Rows: Start Time (defaults now, tap to edit), Formula Brand (picker, sticky last value; seed with Enfamil Neuropro and Good Start Plus, allow add), Amount, Notes.

**Departure from Nara:** Amount is chips for the four most recent distinct amounts (currently 35, 45, 75, 125) plus a numeric keypad; Nara's "Use last amount? Yes" becomes the pre-selected chip. No photo.

Tap budget for a repeat feed: 1 (Same again). Via sheet: + → Bottle → Save = 3 with defaults pre-filled.

### 5.4 Diaper sheet — IMG_2604
Cream title bar. Rows: Time; three circle toggles wet / dirty / dry (multi-select); collapsible "Texture & Color" section — a row of five texture chips with small icons, a row of six colour swatches — auto-expanded when dirty is selected, pre-lit with the last-used texture and colour; Blowout toggle; Diaper Rash toggle; Notes.

Copy this screen almost verbatim. Tap budget: + → wet → Save = 3. Dirty with remembered detail: + → dirty → Save = 3.

### 5.5 Breastfeed sheet with timer — no screenshot; inferred from the export and Nara's store listing
Yellow title bar. Two large side buttons, **Left** and **Right**, showing the side that ended the last feed (Nara highlights it). Tapping a side starts the timer for that side; tapping the other side switches; tapping the running side pauses. Big serif elapsed display per side and total. Below: Start Time, editable Left / Right duration fields (typing here sets `manual: true`), Notes. Save closes the timer (`ended_at = now`).

Timer mechanics in §6.

### 5.6 Pump sheet — no screenshot
Salmon title bar. Same timer control as breastfeed, plus Left mL and Right mL fields entered at the end. Pump entries have `child_id = null`.

### 5.7 Summary sheet — IMG_2596, IMG_2597, IMG_2593
Slides up from the header button. Today / Last 24 Hours segmented control. One row per type with a count badge and totals: breastfeed (total, left, right minutes), bottle (total mL, split by breast milk / formula), diaper (n wet, n dirty), pump (total, left, right mL). Today = local midnight to now; Last 24 Hours = now − 24h.

**Addition:** if any diaper in the window has colour red, black (after day 7), or gray, show a small flag on the diaper row.

### 5.8 History
Day list, most recent first; tap a day for its full chronological log across all types. Nara's version is unscreenshotted; a plain list is fine.

### 5.9 Growth — IMG_2599, IMG_2600 — phase 4
Weight / Height / Head Size segmented tabs. WHO percentile curves (2, 5, 10, 25, 50, 75, 90, 95, 98) in gold, 50th thicker; child's measurements as blue connected dots, latest ringed; tap a point for a callout with value, percentile, age. Curves from WHO LMS tables (girls, by day for 0–13 weeks then by month). x-axis in days for the first months.

## 6. Timer

A running timer is an `entries` row with `ended_at IS NULL`. Elapsed time is always `now − started_at` minus paused segments, computed on the client from `payload.segments`; there is no client-side interval state to lose. Any device in the household sees the running timer via Realtime and can stop it. Only one running breastfeed and one running pump per household; enforce with a partial unique index.

Because a PWA has no lock-screen timer:
- Keep the screen awake while the timer sheet is open (`navigator.wakeLock`).
- Show the running timer as a persistent banner at the top of every screen with elapsed time and a Stop button.
- Optional: a Supabase Edge Function exposing `POST /timer/start?side=left` and `/timer/stop` for iOS Shortcuts, so "Hey Siri, start left" works with the phone in a pocket. Auth via a per-caregiver bearer token.

Export evidence that the timer may not be load-bearing: by mid-September roughly 90% of breastfeed durations were round minutes (typed after the fact), versus under 10% in mid-August. Confirm with Dominique whether a better timer would get used before polishing it; ship manual duration entry first either way.

## 7. Design system

Approximate values sampled from screenshots; tune by eye.

```css
:root {
  --bg: #1b2030;           /* page background */
  --card: #232938;         /* card body, sheet body */
  --rule: #343b4c;         /* hairline dividers */
  --text: #f2f3f5;
  --muted: #a3a9b8;        /* timestamps, secondary labels */
  --accent: #4b6b9b;       /* + buttons, selected toggles */
  --feed: #f5c842;         /* Feed band, feed icons */
  --diaper: #efe8d8;       /* Diaper band */
  --pump: #f2a59b;         /* Pump band */
  --growth: #b9e39a;       /* Growth band */
  --breastfeed-badge: #f5c842;
  --bottle-badge: #f0a03c;
}
```

- Type: Fraunces 600 for card titles (~28px), sheet titles (~30px), row labels in sheets (~22px), and headline values (~52px numerals with a small `mL` superscript); Inter for body, timestamps, chips.
- Card: 16px radius, band ~52px tall, band title left-aligned; + button 56px circle, `--accent`, centred on the band's bottom edge, 24px from the right.
- Sheets: full-width bottom sheet, 20px top radius, title bar in the type colour with dark text.
- Toggles: 90px outlined circles, `--accent` fill when selected.
- Log bar: 6px tall, rounded, type colour; width = value / max(day) × available width.
- Tab bar: Activity, History, Summary. (Nara has Trends, Guides, Account; drop the first two, Account is a settings gear.)
- Dark theme only for v1; both caregivers run dark mode.

## 8. UX rules

1. Every sheet opens with all fields defaulted so Save is valid immediately.
2. Time defaults to now and is one tap to change; never require it.
3. Repeat of the previous entry is one tap from the Activity screen.
4. Every write gets an undo toast; deletes are soft (`deleted_at`) for 30 days.
5. Entries show `created_by` initial subtly so the two of you can tell who logged what.
6. No confirmation dialogs anywhere; undo instead.
7. Mistap protection: the + buttons and Same-again need ≥ 48px targets and 8px spacing.

## 9. Build order

- **Phase 0 (2–3 h):** repo, Supabase project, schema + RLS, two caregiver accounts, import script, acceptance test in §4 passes.
- **Phase 1 (4–6 h):** Activity screen with Feed + Diaper cards, Bottle and Diaper sheets, Same-again, Realtime sync, PWA install. This is usable; start running it in parallel with Nara here.
- **Phase 2 (2–3 h):** Breastfeed sheet with timer and manual entry, Pump sheet and card, running-timer banner, wake lock.
- **Phase 3 (2–3 h):** Summary sheet, History, edit/delete with undo, offline write queue.
- **Phase 4 (optional, ~4 h):** Growth chart with WHO LMS, Siri Shortcut endpoints, Combo Feed sheet.

Re-run the import immediately before cutover, then stop logging in Nara.

## 10. Open questions

- Nara's subscription price and what stays free — screenshot the in-app notice; it changes the build-vs-pay calculus.
- Does Dominique want a live timer, or is typed duration the real workflow now?
- Apple sign-in vs magic link: Apple is smoother on iPhone, magic link is less setup.
- Who pumps and where does the pumped volume feed the bottle log? (No link in Nara; probably not worth modelling.)

## 11. Reference screenshots

| File | Shows |
|---|---|
| IMG_2593 | Summary — Last 24 Hours (9:36am snapshot) |
| IMG_2594 | Activity — Feed card expanded, log rows with bars |
| IMG_2595 | Activity — Diaper, Pump, Growth cards, tab bar |
| IMG_2596 | Summary — Last 24 Hours (12:30pm snapshot; acceptance fixture) |
| IMG_2597 | Summary — Today (12:30pm snapshot; acceptance fixture) |
| IMG_2598 | Activity — Feed, Diaper, Pump cards collapsed |
| IMG_2599 | Growth — Weight, WHO curves |
| IMG_2600 | Growth — Head Size, WHO curves |
| IMG_2601 | Feed type picker sheet |
| IMG_2602 | Bottle Feed sheet — "use last amount" prompt |
| IMG_2603 | Bottle Feed sheet — formula selected |
| IMG_2604 | Diaper sheet — texture and colour chips |
