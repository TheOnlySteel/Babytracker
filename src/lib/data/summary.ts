import type {
  BottlePayload,
  BreastfeedPayload,
  DiaperPayload,
  Entry,
  PumpPayload,
} from './types';
import { householdDay, localParts } from './sleep-time';
import { isFeed, pumpTotalMl, type SleepPayload } from './types';
import { runningElapsed } from './derive';

export interface Summary {
  sleep: {
    naps: number;
    nap_s: number;
    longest_nap_s: number;
    night_s: number;
    sleep_s: number;
    uncertain: boolean;
  };
  breastfeed: {
    count: number;
    total_s: number;
    left_s: number;
    right_s: number;
  };
  bottle: {
    count: number;
    total_ml: number;
    breast_milk_ml: number;
    formula_ml: number;
  };
  diaper: {
    count: number;
    wet: number;
    dirty: number;
    dry: number;
    flagged: boolean;
  };
  pump: { count: number; total_ml: number; left_ml: number; right_ml: number };
}

/** Local midnight for the given instant (device timezone). */
export function startOfDay(d: Date): Date {
  const x = new Date(d);
  x.setHours(0, 0, 0, 0);
  return x;
}

export function windowFor(
  mode: 'today' | '24h' | 'last-night',
  now: Date,
  timezone?: string,
  entries: Entry[] = [],
): { from: Date; to: Date } {
  if (mode === 'last-night') {
    const key = latestNightKey(entries, now, timezone);
    const n = key ? nightSummary(entries, key, now) : null;
    return n?.from && n.to
      ? { from: n.from, to: n.to }
      : { from: now, to: now };
  }
  return {
    from:
      mode === 'today'
        ? timezone
          ? householdDay(now, timezone)
          : startOfDay(now)
        : new Date(+now - 86400000),
    to: now,
  };
}

export function inWindow(e: Entry, from: Date, to: Date): boolean {
  const t = new Date(e.started_at).getTime();
  return t >= from.getTime() && t <= to.getTime();
}

/**
 * @param to       end of the aggregation window (may be in the future, e.g. History's next midnight)
 * @param birthDate used for the black-stool flag (meconium is normal in the first week)
 * @param now      the clock for a still-running timer; open segments are counted up to
 *                 min(now, to), never into the future. Defaults to `to` for callers whose window ends now.
 */
export function summarize(
  entries: Entry[],
  from: Date,
  to: Date,
  birthDate?: string,
  now?: Date,
): Summary {
  const s: Summary = {
    sleep: {
      naps: 0,
      nap_s: 0,
      longest_nap_s: 0,
      night_s: 0,
      sleep_s: 0,
      uncertain: false,
    },
    breastfeed: { count: 0, total_s: 0, left_s: 0, right_s: 0 },
    bottle: { count: 0, total_ml: 0, breast_milk_ml: 0, formula_ml: 0 },
    diaper: { count: 0, wet: 0, dirty: 0, dry: 0, flagged: false },
    pump: { count: 0, total_ml: 0, left_ml: 0, right_ml: 0 },
  };
  const day7 = birthDate ? new Date(birthDate).getTime() + 7 * 86400 * 1000 : 0;
  const clock = new Date(Math.min((now ?? to).getTime(), to.getTime()));

  for (const e of entries) {
    if (e.deleted_at) continue;
    if (e.type === 'sleep') {
      const p = e.payload as SleepPayload;
      const seconds = Math.max(
        0,
        (Math.min(Date.parse(e.ended_at ?? clock.toISOString()), +clock) -
          Math.max(Date.parse(e.started_at), +from)) /
          1000,
      );
      s.sleep.sleep_s += seconds;
      if (p.kind === 'nap') {
        s.sleep.nap_s += seconds;
        // "Longest" is the nap's own length, not the part that overlaps the window.
        const full = Math.max(0, (Math.min(Date.parse(e.ended_at ?? clock.toISOString()), +clock) - Date.parse(e.started_at)) / 1000);
        if (seconds > 0) s.sleep.longest_nap_s = Math.max(s.sleep.longest_nap_s, full);
        if (Date.parse(e.started_at) >= +from && Date.parse(e.started_at) < +to)
          s.sleep.naps++;
      } else s.sleep.night_s += seconds;
      if (seconds > 0 && (p.uncertain_end || p.provisional))
        s.sleep.uncertain = true;
      continue;
    }
    if (!inWindow(e, from, to)) continue;
    switch (e.type) {
      case 'breastfeed':
      case 'combo': {
        const p = e.payload as BreastfeedPayload;
        s.breastfeed.count++;
        if (e.ended_at === null && p.segments?.length) {
          // Running timer: count what has elapsed so far, so Summary agrees with the clock.
          const el = runningElapsed(p, clock);
          s.breastfeed.left_s += el.left_s;
          s.breastfeed.right_s += el.right_s;
        } else {
          s.breastfeed.left_s += p.left_s ?? 0;
          s.breastfeed.right_s += p.right_s ?? 0;
        }
        if (e.type === 'combo') {
          const b = e.payload as BottlePayload;
          if ((b.breast_milk_ml ?? 0) + (b.formula_ml ?? 0) > 0) {
            s.bottle.count++;
            s.bottle.breast_milk_ml += b.breast_milk_ml ?? 0;
            s.bottle.formula_ml += b.formula_ml ?? 0;
          }
        }
        break;
      }
      case 'bottle': {
        const p = e.payload as BottlePayload;
        s.bottle.count++;
        s.bottle.breast_milk_ml += p.breast_milk_ml ?? 0;
        s.bottle.formula_ml += p.formula_ml ?? 0;
        break;
      }
      case 'diaper': {
        const p = e.payload as DiaperPayload;
        s.diaper.count++;
        if (p.wet) s.diaper.wet++;
        if (p.dirty) s.diaper.dirty++;
        if (p.dry) s.diaper.dry++;
        const t = new Date(e.started_at).getTime();
        for (const c of p.color ?? []) {
          if (c === 'red' || c === 'gray' || (c === 'black' && t > day7))
            s.diaper.flagged = true;
        }
        break;
      }
      case 'pump': {
        const p = e.payload as PumpPayload;
        s.pump.count++;
        s.pump.total_ml += pumpTotalMl(p);
        s.pump.left_ml += p.left_ml ?? 0;
        s.pump.right_ml += p.right_ml ?? 0;
        break;
      }
    }
  }
  s.breastfeed.total_s = s.breastfeed.left_s + s.breastfeed.right_s;
  s.bottle.total_ml = s.bottle.breast_milk_ml + s.bottle.formula_ml;
  return s;
}

/** Number of feeding sessions (breastfeed, bottle or combo), each counted once. */
export function feedSessions(entries: Entry[]): number {
  return entries.filter((e) => !e.deleted_at && isFeed(e.type)).length;
}

/** Entries of a card family (feed = breastfeed+bottle+combo), newest first. */
export function ofCard(
  entries: Entry[],
  card: 'feed' | 'diaper' | 'pump' | 'growth' | 'sleep',
): Entry[] {
  return entries
    .filter(
      (e) =>
        !e.deleted_at && (card === 'feed' ? isFeed(e.type) : e.type === card),
    )
    .sort((a, b) => b.started_at.localeCompare(a.started_at));
}

/** Group entries by local calendar day key (YYYY-MM-DD), newest day first. A row lives under
 *  the day it started, a night sleep included; the day's totals still count the overlap. */
export function groupByDay(
  entries: Entry[],
  timezone?: string,
): { day: string; entries: Entry[] }[] {
  const map = new Map<string, Entry[]>();
  for (const e of entries) {
    if (e.deleted_at) continue;
    const key = timezone
      ? localParts(new Date(e.started_at), timezone).date
      : dayKey(new Date(e.started_at));
    if (!map.has(key)) map.set(key, []);
    map.get(key)!.push(e);
  }
  return [...map.entries()]
    .sort((a, b) => b[0].localeCompare(a[0]))
    .map(([day, es]) => ({
      day,
      entries: es.sort((a, b) => b.started_at.localeCompare(a.started_at)),
    }));
}

export function dayKey(d: Date): string {
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${y}-${m}-${day}`;
}

export function latestNightKey(
  entries: Entry[],
  now: Date,
  timezone?: string,
): string | undefined {
  const date = timezone ? localParts(now, timezone).date : dayKey(now);
  return entries
    .filter((e) => !e.deleted_at && e.type === 'sleep')
    .map((e) => (e.payload as SleepPayload).night_key)
    .filter((k): k is string => !!k && k < date)
    .sort()
    .at(-1);
}
export function nightSummary(entries: Entry[], key: string, now = new Date()) {
  const rows = entries
    .filter(
      (e) =>
        !e.deleted_at &&
        e.type === 'sleep' &&
        (e.payload as SleepPayload).night_key === key,
    )
    .sort((a, b) => a.started_at.localeCompare(b.started_at));
  // Union overlapping intervals: manual corrections cannot invent a waking or double count minutes.
  const spans: Array<[number, number]> = [];
  for (const e of rows) {
    const start = Date.parse(e.started_at),
      end = Math.min(Date.parse(e.ended_at ?? now.toISOString()), +now);
    if (end <= start) continue;
    const last = spans.at(-1);
    if (last && start <= last[1]) last[1] = Math.max(last[1], end);
    else spans.push([start, end]);
  }
  return {
    from: spans.length ? new Date(spans[0][0]) : null,
    to: spans.length ? new Date(spans.at(-1)![1]) : null,
    asleep_s: spans.reduce((n, [a, b]) => n + (b - a) / 1000, 0),
    wakings: Math.max(0, spans.length - 1),
    rows,
  };
}
