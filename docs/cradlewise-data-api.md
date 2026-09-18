# Cradlewise Data API v1 — reference for this project

Condensed from the official documentation at https://integrations.cradlewise.com/documentation (page dated 2026-09-17, API "last updated July 16, 2026", beta). Kept here because the poller's parsers are written against these shapes and the documentation site is not reachable from every environment we work in. Beta caveat from Cradlewise: new fields may appear at any time and must be ignored; breaking changes will be announced in the changelog where possible.

## Access

- Base URL `https://integrations.cradlewise.com/api/v1`, HTTPS only, every endpoint is GET, success is 200 with JSON.
- `Authorization: Bearer cw_<40 chars>`. One token per baby, created by an admin on the web dashboard under "Cradlewise Data API"; valid 60 days; generating a new one disables the old one; requires Nurture Plus (lapsed subscription → 403 until it resumes, token not deleted).
- Errors are `{ "detail": "..." }`: 401 missing/invalid/expired token, 403 subscription inactive, 404 wrong path, 422 missing or malformed `start_time`/`end_time`, 429 rate limited, 500 retry with backoff.

## Time parameters

`start_time` and `end_time` use `YYYY-MM-DD HH:MM:SS` in the **baby's local time**. A `T` separator fails with 422. In a query string the space **must** be `%20` (a `+` is not the documented form). Responses are not paginated; keep ranges to a week or a month. Responses carry a `timezone` (IANA) that governs every local timestamp in them.

**Day start time.** A "day" runs from the baby's day-start setting (default 08:00) to the same time next date, so a night that runs past midnight stays with the evening it began. Responses include `day_start_time`; historical data uses the setting active at the time.

## Rate limits (rolling windows, per token)

| Scope | Limit |
| --- | --- |
| Status endpoints | 2 requests / minute |
| Sleep endpoints | 60 requests / hour |
| All endpoints | 2,880 requests / day |

Every response carries `X-RateLimit-Remaining` and `X-RateLimit-Reset` (Unix timestamp when the oldest counted request leaves the window; **not** a daily reset). A 429 carries `Retry-After` in seconds (≤ 60 for status). Polling status every 30 s uses the entire daily budget; anything else must come out of it.

## GET /baby/status

```json
{
  "status": "sleeping",
  "since": "2026-03-12T22:15:00Z",
  "is_in_crib": true,
  "crib_mode": { "bounce": "on", "music": "off" },
  "timestamp": "2026-03-13T01:30:00Z"
}
```

| Field | Notes |
| --- | --- |
| `status` | `sleeping` · `awake` · `stirring` · `crying` · `away` |
| `since` | UTC ISO 8601, when the current status began; may carry fractional seconds |
| `is_in_crib` | false when away |
| `crib_mode.bounce`, `crib_mode.music` | `on` / `off`, read-only |
| `timestamp` | UTC, when the API generated the response ("standard backend latency" applies) |

## GET /sleep/c-chart?start_time&end_time — sleep sessions

```json
{
  "timezone": "America/Los_Angeles",
  "day_start_time": "2026-03-10 08:00:00.000000",
  "sessions": [
    { "session_id": "sess_0", "start_time": "2026-03-10 08:00:00.000000", "end_time": "2026-03-11 08:00:00.000000",
      "header": "Baby was in bed for 17h 2m, slept 15h 30m", "is_user_added": false,
      "total_time_in_crib_in_seconds": 61276.7, "total_time_asleep_in_seconds": 55800.0, "total_time_awake_in_seconds": 5476.7 }
  ],
  "events": [
    { "event_name": "deep_sleep", "event_label": "sleep", "event_value": "5", "event_time": "2026-03-10 22:15:00.000000", "is_user_added": false }
  ],
  "soothe_events": [],
  "day_aggregates": {
    "2026-03-10 08:00:00.000000": { "total_time_in_crib_in_seconds": 61276.7, "total_time_asleep_in_seconds": 55800.0,
      "total_time_awake_in_seconds": 5476.7, "total_day_sleep": 12600.0, "total_night_sleep": 43200.0 }
  },
  "video_history_data": []
}
```

Sessions are **crib stays** with asleep and awake totals inside them, not sleep intervals. Sleep intervals come from `events`; the documentation shows `event_label: "sleep"` (with `event_name: "deep_sleep"`) but does not enumerate the full label vocabulary. The poller therefore classifies labels by substring (`sleep`, `stir`, wake-like words) and drops any interval touching an unknown label rather than guessing; unknown labels surface in Settings as `history_error`. `is_user_added` marks caregiver-entered sessions/events.

## GET /sleep/day-metrics?start_time&end_time

One `metrics[]` entry per day in the range, each with seven `banners` always in this order: SOOTHES, RISE TIME, BEDTIME, NAPS, LONGEST STRETCH, TIME IN BED, AWAKE IN BED.

```json
{ "header": "KEY METRICS", "timezone": "America/Los_Angeles",
  "metrics": [ { "date": "2026-03-10 08:00:00.000000", "banners": [
    { "type": "soothes", "header": "SOOTHES", "data": { "value": 3, "display_value": "3", "description": "times soothed" } },
    { "type": "info", "header": "RISE TIME", "data": { "value": "2026-03-11 07:45:00.000000", "display_value": "7:45 am" } },
    { "type": "info", "header": "BEDTIME", "data": { "value": "2026-03-10 20:30:00.000000", "display_value": "8:30 pm" } },
    { "type": "naps", "header": "NAPS", "data": { "value": 2, "display_value": "2", "naps": [ { "start_time": "...", "end_time": "...", "duration_in_mins": 45 } ] } },
    { "type": "info", "header": "LONGEST STRETCH", "data": { "value": 32400, "display_value": "9h 0m" } },
    { "type": "info", "header": "TIME IN BED", "data": { "value": 612, "display_value": "10h 12m" } },
    { "type": "info", "header": "AWAKE IN BED", "data": { "value": 5476, "display_value": "1h 31m" } } ] } ] }
```

Raw `value` units differ per banner: LONGEST STRETCH and AWAKE IN BED in seconds, TIME IN BED in minutes, SOOTHES and NAPS counts, RISE TIME and BEDTIME local timestamps. `display_value` is for people. The NAPS banner's `naps[]` is a per-nap list with local start and end times; the poller stores it with the day metrics as a cross-check for derived naps.

## GET /sleep/weekly-sleep-metrics and /sleep/monthly-sleep-metrics

Not used by the poller. Weekly returns `sleep_graph_metrics` (averages and per-day day/night/total minutes) and `nap_planner_metrics` (each session with `is_night_sleep`, `is_longest_stretch`, `duration_in_mins`). Monthly returns per-age-month averages where `month` is the baby's age label (`"0M"` = first month of life), not a calendar month.

## What the poller does with this

- Status every 30 s (60 s while settled asleep or away), admitted through the rolling ledger in `cw_admit`; `429` and `X-RateLimit-Reset` pauses are honoured and capped at one hour.
- Day metrics at most every 30 minutes; a parse or 4xx problem on this endpoint is recorded as `metrics_error` and never pauses status polling.
- c-chart at most hourly and once after rise time, from the 200-request reserve; intervals derived from `events` reconcile provisional sleep rows (`history_error` records unknown labels or dropped intervals without stopping the batch).
- Nothing here is captured production data. The parser tests use synthetic payloads in the documented shapes; the first live responses should be checked against this page and the tests updated if anything differs.
