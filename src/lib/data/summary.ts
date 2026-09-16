import type { BottlePayload, BreastfeedPayload, DiaperPayload, Entry, PumpPayload } from './types';
import { isFeed } from './types';

export interface Summary {
  breastfeed: { count: number; total_s: number; left_s: number; right_s: number };
  bottle: { count: number; total_ml: number; breast_milk_ml: number; formula_ml: number };
  diaper: { count: number; wet: number; dirty: number; dry: number; flagged: boolean };
  pump: { count: number; total_ml: number; left_ml: number; right_ml: number };
}

/** Local midnight for the given instant (device timezone). */
export function startOfDay(d: Date): Date {
  const x = new Date(d);
  x.setHours(0, 0, 0, 0);
  return x;
}

export function windowFor(mode: 'today' | '24h', now: Date): { from: Date; to: Date } {
  return mode === 'today'
    ? { from: startOfDay(now), to: now }
    : { from: new Date(now.getTime() - 24 * 3600 * 1000), to: now };
}

export function inWindow(e: Entry, from: Date, to: Date): boolean {
  const t = new Date(e.started_at).getTime();
  return t >= from.getTime() && t <= to.getTime();
}

/**
 * @param birthDate used for the black-stool flag (meconium is normal in the first week)
 */
export function summarize(entries: Entry[], from: Date, to: Date, birthDate?: string): Summary {
  const s: Summary = {
    breastfeed: { count: 0, total_s: 0, left_s: 0, right_s: 0 },
    bottle: { count: 0, total_ml: 0, breast_milk_ml: 0, formula_ml: 0 },
    diaper: { count: 0, wet: 0, dirty: 0, dry: 0, flagged: false },
    pump: { count: 0, total_ml: 0, left_ml: 0, right_ml: 0 }
  };
  const day7 = birthDate ? new Date(birthDate).getTime() + 7 * 86400 * 1000 : 0;

  for (const e of entries) {
    if (e.deleted_at || !inWindow(e, from, to)) continue;
    switch (e.type) {
      case 'breastfeed':
      case 'combo': {
        const p = e.payload as BreastfeedPayload;
        s.breastfeed.count++;
        s.breastfeed.left_s += p.left_s ?? 0;
        s.breastfeed.right_s += p.right_s ?? 0;
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
          if (c === 'red' || c === 'gray' || (c === 'black' && t > day7)) s.diaper.flagged = true;
        }
        break;
      }
      case 'pump': {
        const p = e.payload as PumpPayload;
        s.pump.count++;
        s.pump.left_ml += p.left_ml ?? 0;
        s.pump.right_ml += p.right_ml ?? 0;
        break;
      }
    }
  }
  s.breastfeed.total_s = s.breastfeed.left_s + s.breastfeed.right_s;
  s.bottle.total_ml = s.bottle.breast_milk_ml + s.bottle.formula_ml;
  s.pump.total_ml = s.pump.left_ml + s.pump.right_ml;
  return s;
}

/** Entries of a card family (feed = breastfeed+bottle+combo), newest first. */
export function ofCard(entries: Entry[], card: 'feed' | 'diaper' | 'pump' | 'growth'): Entry[] {
  return entries
    .filter((e) => !e.deleted_at && (card === 'feed' ? isFeed(e.type) : e.type === card))
    .sort((a, b) => b.started_at.localeCompare(a.started_at));
}

/** Group entries by local calendar day key (YYYY-MM-DD), newest day first. */
export function groupByDay(entries: Entry[]): { day: string; entries: Entry[] }[] {
  const map = new Map<string, Entry[]>();
  for (const e of entries) {
    if (e.deleted_at) continue;
    const k = dayKey(new Date(e.started_at));
    if (!map.has(k)) map.set(k, []);
    map.get(k)!.push(e);
  }
  return [...map.entries()]
    .sort((a, b) => b[0].localeCompare(a[0]))
    .map(([day, es]) => ({ day, entries: es.sort((a, b) => b.started_at.localeCompare(a.started_at)) }));
}

export function dayKey(d: Date): string {
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${y}-${m}-${day}`;
}
