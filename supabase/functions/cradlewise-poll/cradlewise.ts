import { localInstant, localParts, sourceKey, sleepKind, validTimezone } from '../_shared/sleep-time.ts';
import type { Action, Context, SleepRow } from './derive-sleep.ts';

/**
 * Parsers for the Cradlewise Data API v1 (docs/cradlewise-data-api.md). The documentation
 * promises that new fields may appear at any time and that integrations must ignore them, so
 * every parser reads only what it needs, tolerates missing optional fields, and rejects only
 * what would make the data meaningless.
 */

const STATUSES = ['sleeping', 'awake', 'stirring', 'crying', 'away'];

const record = (v: unknown): Record<string, any> => {
  if (!v || typeof v !== 'object' || Array.isArray(v)) throw new Error('Expected object');
  return v as Record<string, any>;
};

/** A UTC instant such as `2026-03-12T22:15:00Z` (fractional seconds allowed). Returns null when absent or unparsable. */
const instant = (v: unknown): string | null => {
  if (typeof v !== 'string' || !/(Z|[+-]\d\d:\d\d)$/.test(v) || !Number.isFinite(Date.parse(v))) return null;
  return new Date(v).toISOString();
};

/** GET /baby/status → one observation. `since` is required; everything else degrades gracefully. */
export function parseStatus(raw: unknown, receivedAt: string) {
  const r = record(raw);
  // An undocumented status value is recorded as unknown rather than dropping the poll.
  const status = STATUSES.includes(r.status) ? (r.status as string) : 'unknown';
  const since = instant(r.since);
  if (!since) throw new Error('Missing since');
  const upstream = instant(r.timestamp) ?? receivedAt;
  if (Date.parse(upstream) > Date.parse(receivedAt) + 120000 || Date.parse(since) > Date.parse(upstream) + 60000)
    throw new Error('Future status');
  // A cached response is not a fresh observation. Never advance freshness past upstream time.
  const observed_at = new Date(Math.min(Date.parse(receivedAt), Date.parse(upstream))).toISOString();
  const mode = r.crib_mode && typeof r.crib_mode === 'object' ? (r.crib_mode as Record<string, unknown>) : {};
  return {
    status,
    since,
    bounce: typeof mode.bounce === 'string' ? mode.bounce : null,
    music: typeof mode.music === 'string' ? mode.music : null,
    upstream_at: upstream,
    observed_at,
    raw
  };
}

export interface HistorySleep {
  start: string;
  end: string;
}

/**
 * An offsetless API timestamp ("YYYY-MM-DD HH:MM:SS.ffffff") as an instant. The documentation
 * calls these local, but the live API sends them in UTC: the last c-chart event of the first
 * captured response was byte-for-byte the status endpoint's UTC `since`, and the session that
 * began at "05:58:26" was the BEDTIME banner displayed as "10:58 pm" Pacific. An explicit offset
 * or Z is honoured if one ever appears. Returns null for anything else.
 */
export function apiInstant(v: unknown): string | null {
  if (typeof v !== 'string') return null;
  const m = v.trim().match(/^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2}:\d{2})(\.\d+)?(Z|[+-]\d\d:?\d\d)?$/);
  if (!m) return null;
  const d = new Date(`${m[1]}T${m[2]}${m[3] ?? ''}${m[4] ?? 'Z'}`);
  return Number.isFinite(+d) ? d.toISOString() : null;
}

/** Parses a display string such as "10:58 pm" or "7:45 am" to minutes after local midnight. */
function displayMinute(v: unknown): number | null {
  if (typeof v !== 'string') return null;
  const m = v.trim().match(/^(\d{1,2})(?::(\d{2}))?\s*([ap])\.?m\.?$/i);
  if (!m) return null;
  const h = Number(m[1]) % 12,
    min = Number(m[2] ?? 0);
  return (m[3].toLowerCase() === 'p' ? h + 12 : h) * 60 + min;
}

/**
 * A raw banner timestamp as an instant. Live responses carry them as offsetless UTC with a local
 * `display_value` beside them (see apiInstant). Both readings are still tried and the one that
 * agrees with the display string wins, so a future switch to local time cannot shift bed and rise
 * by the zone offset; with no display string, UTC is assumed.
 */
function bannerInstant(raw: unknown, display: unknown, tz: string): Date | null {
  if (typeof raw !== 'string') return null;
  const m = raw.match(/^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2}:\d{2})/);
  if (!m) return null;
  const asUtc = new Date(`${m[1]}T${m[2]}Z`);
  let asLocal: Date | null = null;
  try {
    asLocal = localInstant(`${m[1]} ${m[2]}`, tz, 'strict');
  } catch {
    asLocal = null;
  }
  const want = displayMinute(display);
  if (want !== null) {
    for (const d of [asUtc, asLocal]) if (d && localParts(d, tz).minute === want) return d;
  }
  return Number.isFinite(+asUtc) ? asUtc : asLocal;
}

/**
 * GET /sleep/day-metrics → bed and rise minutes in the household zone, plus the raw banners.
 * The most recent day in the response is used. The response's own `timezone` governs its display
 * strings; the household zone only decides the minute-of-day for bed and rise. The NAPS banner
 * lists naps as display strings ("2:59 pm"), so it is kept for people, not parsed into intervals.
 */
export function parseMetrics(raw: unknown, householdTz: string) {
  const r = record(raw);
  if (!Array.isArray(r.metrics)) throw new Error('Metrics shape');
  const tz = validTimezone(r.timezone) ? r.timezone : householdTz;
  const days = [...r.metrics].filter((d) => d && Array.isArray(d.banners)).sort((a, b) => String(b.date).localeCompare(String(a.date)));
  if (!days.length) throw new Error('Missing banners');
  const banners: any[] = days[0].banners;
  const banner = (name: string) => banners.find((b) => b && typeof b.header === 'string' && b.header.toUpperCase() === name);
  const value = (name: string) => banner(name)?.data?.value;
  const minute = (name: string) => {
    const at = bannerInstant(value(name), banner(name)?.data?.display_value, tz);
    return at ? localParts(at, householdTz).minute : null;
  };
  const naps: HistorySleep[] = [];
  for (const n of Array.isArray(banner('NAPS')?.data?.naps) ? banner('NAPS').data.naps : []) {
    const start = apiInstant(n?.start_time),
      end = apiInstant(n?.end_time);
    if (start && end && end > start) naps.push({ start, end });
  }
  const awake = value('AWAKE IN BED');
  return {
    bed_min: minute('BEDTIME'),
    rise_min: minute('RISE TIME'),
    day_metrics: { raw, awake_in_bed_s: typeof awake === 'number' ? awake : null, naps }
  };
}

export interface HistoryParse {
  sleeps: HistorySleep[];
  /** Labels the parser did not understand. Intervals touching them were dropped, not guessed. */
  unknownLabels: string[];
  droppedIntervals: number;
}

/** Event labels: anything containing "sleep" is asleep, "stir" continues, the rest ends a sleep. */
function classify(label: unknown): 'sleep' | 'stir' | 'wake' | 'unknown' {
  if (typeof label !== 'string') return 'unknown';
  const l = label.toLowerCase();
  if (l.includes('sleep')) return 'sleep';
  if (l.includes('stir')) return 'stir';
  if (['awake', 'wake', 'woke', 'away', 'crying', 'cry', 'out_of_crib', 'pickup'].some((k) => l.includes(k))) return 'wake';
  return 'unknown';
}

/**
 * GET /sleep/c-chart → sleep intervals from the raw events. Session totals are crib stays, not
 * sleep, so only events are used. Event times are UTC (apiInstant); the response's `timezone`
 * governs only its display strings, so the household zone plays no part here. An interval that
 * touches a user-added event, an unknown label or a malformed timestamp is dropped on its own;
 * the rest of the batch still reconciles. Live vocabulary: event_label sleep / stirring / awake /
 * away over event_name deep_sleep / light_sleep / quiet_awake / active_awake / away.
 */
export function parseHistory(raw: unknown, _householdTz: string): HistoryParse {
  const r = record(raw);
  if (!Array.isArray(r.events)) throw new Error('History shape');
  const unknown = new Set<string>();
  let dropped = 0;
  type Ev = { at: string | null; kind: ReturnType<typeof classify>; user: boolean; label: string };
  const events: Ev[] = r.events
    .filter((v: unknown) => v && typeof v === 'object')
    .map((v: any) => ({
      at: apiInstant(v.event_time),
      kind: classify(v.event_label ?? v.event_name),
      user: v.is_user_added === true,
      label: String(v.event_label ?? v.event_name ?? '')
    }))
    .sort((a: Ev, b: Ev) => (a.at ?? '').localeCompare(b.at ?? ''));
  const sleeps: HistorySleep[] = [];
  let start: string | null = null;
  let tainted = false;
  for (const e of events) {
    if (e.kind === 'unknown') unknown.add(e.label);
    const bad = e.kind === 'unknown' || e.user || !e.at;
    if (bad) {
      if (start) tainted = true; // the open interval now depends on something we do not trust
      continue;
    }
    if (e.kind === 'sleep') {
      if (!start) {
        start = e.at;
        tainted = false;
      }
    } else if (e.kind === 'wake' && start) {
      if (tainted) dropped++;
      else if (e.at! > start) sleeps.push({ start, end: e.at! });
      start = null;
      tainted = false;
    }
  }
  // A trailing sleep has no authoritative end yet.
  return { sleeps, unknownLabels: [...unknown], droppedIntervals: dropped };
}

/**
 * `from`, when given, is the start of the window the caller loaded `rows` for. Intervals that
 * begin earlier are not acted on: their rows may not be loaded, so they would be inserted a
 * second time (or resurrect a dismissed sleep). They still count as overlaps, so a row that
 * straddles the boundary is never reshaped to the in-window part only.
 */
export function reconcileSleep(history: HistorySleep[], rows: SleepRow[], ctx: Context, from?: string): Action[] {
  const actions: Action[] = [];
  const used = new Set<string>();
  for (const h of history) {
    if (from && Date.parse(h.start) < Date.parse(from)) continue;
    const key = sourceKey(h.start);
    const matching = rows.filter(
      (e) => e.source_key === key || (Date.parse(e.started_at) < Date.parse(h.end) && Date.parse(e.ended_at ?? ctx.now) > Date.parse(h.start))
    );
    // Human edits, tombstones and suppressions always win. Multi-row joins are deliberately
    // left provisional until a human resolves annotations/ownership; never discard them.
    if (rows.some((e) => e.payload.suppressed_source_key === key) || matching.some((e) => e.deleted_at || e.payload.timing_locked || e.payload.source === 'manual'))
      continue;
    if (matching.length > 1) continue;
    const e = matching[0];
    if (e) {
      if (used.has(e.id)) continue; // no destructive split of one row into multiple sessions
      const hits = history.filter((x) => Date.parse(e.started_at) < Date.parse(x.end) && Date.parse(e.ended_at ?? ctx.now) > Date.parse(x.start));
      if (hits.length > 1) continue;
      used.add(e.id);
      // The crib still reports sleep: a completed c-chart interval here means a short wake the
      // status polls never saw, and the sleep after it has no end yet. Ending the row would stop
      // the running sleep (derivation cannot reopen the same key), so only its start is
      // corrected; live derivation closes it.
      if (!e.ended_at && (ctx.status === 'sleeping' || ctx.status === 'stirring')) {
        if (e.started_at !== h.start) actions.push({ op: 'update', id: e.id, version: e.updated_at, started_at: h.start });
        continue;
      }
      if (e.started_at === h.start && e.ended_at === h.end && e.payload.provisional === false) continue;
      actions.push({ op: 'update', id: e.id, version: e.updated_at, started_at: h.start, ended_at: h.end, payload: { provisional: false, uncertain_end: false } });
    } else
      actions.push({
        op: 'insert',
        source_key: key,
        started_at: h.start,
        ended_at: h.end,
        payload: { source: 'cradlewise', place: 'crib', provisional: false, ...sleepKind(h.start, ctx.timezone, ctx.bed_min ?? 1200, ctx.rise_min ?? 480) }
      });
  }
  return actions;
}
