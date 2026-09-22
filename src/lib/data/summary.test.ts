import { describe, it, expect } from 'vitest';
import { summarize, groupByDay, feedSessions, windowFor, dayKeyIn } from './summary';
import { localParts } from './sleep-time';
import type { Entry } from './types';

// Synthetic, privacy-safe fixture. Times are UTC; the assertions below only use explicit windows.
let n = 0;
const mk = (type: Entry['type'], started: string, payload: object, extra: Partial<Entry> = {}): Entry => ({
  id: String(++n),
  household_id: 'h',
  child_id: type === 'pump' ? null : 'c',
  type,
  started_at: started,
  ended_at: null,
  payload: payload as Entry['payload'],
  note: null,
  created_by: 'u',
  updated_by: 'u',
  created_at: started,
  updated_at: started,
  deleted_at: null,
  nara_activity_key: null,
  ...extra
});

const T0 = new Date('2026-09-10T00:00:00Z');
const NOW = new Date('2026-09-10T12:00:00Z');

describe('summarize', () => {
  const entries = [
    mk('bottle', '2026-09-10T08:00:00Z', { kinds: ['formula'], formula_ml: 60 }),
    mk('bottle', '2026-09-10T10:00:00Z', { kinds: ['breast_milk', 'formula'], breast_milk_ml: 30, formula_ml: 40 }),
    mk('breastfeed', '2026-09-10T06:00:00Z', { left_s: 600, right_s: 300, manual: true, segments: [] }, { ended_at: '2026-09-10T06:15:00Z' }),
    mk('combo', '2026-09-10T07:00:00Z', { left_s: 120, right_s: 0, kinds: ['breast_milk'], breast_milk_ml: 20, segments: [] }, { ended_at: '2026-09-10T07:02:00Z' }),
    mk('diaper', '2026-09-10T09:00:00Z', { wet: true, dirty: true, dry: false, texture: ['runny'], color: ['brown'], blowout: false, rash: false }),
    mk('diaper', '2026-09-10T11:00:00Z', { wet: true, dirty: false, dry: false, texture: [], color: [], blowout: false, rash: false }),
    mk('pump', '2026-09-10T05:00:00Z', { left_ml: 40, right_ml: 35 }, { ended_at: '2026-09-10T05:20:00Z' }),
    mk('bottle', '2026-09-09T23:00:00Z', { kinds: ['formula'], formula_ml: 999 }), // outside Today
    mk('bottle', '2026-09-10T11:30:00Z', { kinds: ['formula'], formula_ml: 500 }, { deleted_at: '2026-09-10T11:31:00Z' }) // soft-deleted
  ];

  it('counts and totals within the window, ignoring deleted rows', () => {
    const s = summarize(entries, T0, NOW, '2026-08-01');
    expect(s.bottle).toEqual({ count: 3, total_ml: 150, breast_milk_ml: 50, formula_ml: 100 }); // combo's bottle part counts
    expect(s.breastfeed).toEqual({ count: 2, total_s: 1020, left_s: 720, right_s: 300 });
    expect(s.diaper).toMatchObject({ count: 2, wet: 2, dirty: 1, dry: 0, flagged: false });
    expect(s.pump).toEqual({ count: 1, total_ml: 75, left_ml: 40, right_ml: 35 });
  });

  it('includes elapsed time of a running timer so Summary agrees with the clock', () => {
    const running = mk('breastfeed', '2026-09-10T11:50:00Z', {
      left_s: 0, right_s: 0, manual: false, begin_side: 'left', end_side: null,
      segments: [{ side: 'left', start: '2026-09-10T11:50:00Z', end: '2026-09-10T11:54:00Z' }, { side: 'right', start: '2026-09-10T11:54:00Z', end: null }]
    });
    const s = summarize([running], T0, NOW);
    expect(s.breastfeed).toEqual({ count: 1, total_s: 600, left_s: 240, right_s: 360 });
  });

  it('flags red, gray, and black-after-day-7 stools only', () => {
    const d = (color: string[], when: string) => mk('diaper', when, { wet: false, dirty: true, dry: false, texture: [], color, blowout: false, rash: false });
    expect(summarize([d(['black'], '2026-09-10T08:00:00Z')], T0, NOW, '2026-09-08').diaper.flagged).toBe(false); // day 2: meconium
    expect(summarize([d(['black'], '2026-09-10T08:00:00Z')], T0, NOW, '2026-08-01').diaper.flagged).toBe(true);
    expect(summarize([d(['red'], '2026-09-10T08:00:00Z')], T0, NOW, '2026-09-09').diaper.flagged).toBe(true);
    expect(summarize([d(['yellow', 'brown'], '2026-09-10T08:00:00Z')], T0, NOW, '2026-08-01').diaper.flagged).toBe(false);
  });

  it('counts a combo as one feeding session', () => {
    expect(feedSessions(entries)).toBe(5); // 3 bottles in window + 1 outside + 1 bf + 1 combo, minus the deleted bottle
  });

  it('builds windows from local midnight and 24h back', () => {
    const now = new Date(2026, 8, 10, 12, 30);
    expect(windowFor('today', now).from.getHours()).toBe(0);
    expect(windowFor('24h', now).from.getTime()).toBe(now.getTime() - 86400_000);
  });
});

describe('groupByDay', () => {
  it('groups newest day first and sorts within a day newest first', () => {
    const a = mk('diaper', new Date(2026, 8, 9, 8).toISOString(), {});
    const b = mk('diaper', new Date(2026, 8, 10, 8).toISOString(), {});
    const c = mk('diaper', new Date(2026, 8, 10, 9).toISOString(), {});
    const days = groupByDay([a, b, c]);
    expect(days.map((d) => d.day)).toEqual(['2026-09-10', '2026-09-09']);
    expect(days[0].entries.map((e) => e.id)).toEqual([c.id, b.id]);
  });
});

describe('running timer clock (audit v2 F01)', () => {
  const running = mk('breastfeed', '2026-09-16T18:55:00Z', {
    left_s: 0, right_s: 0, manual: false, begin_side: 'left', end_side: null,
    segments: [{ side: 'left', start: '2026-09-16T18:55:00Z', end: null }]
  });
  const noon = new Date('2026-09-16T19:00:00Z'); // 12:00 PDT
  const midnight = new Date('2026-09-16T07:00:00Z');
  const nextMidnight = new Date('2026-09-17T07:00:00Z');

  it('History (window ending at the next midnight) counts only what has elapsed so far', () => {
    const s = summarize([running], midnight, nextMidnight, undefined, noon);
    expect(s.breastfeed.total_s).toBe(300);
  });

  it('never counts past the window end even if now is later', () => {
    const s = summarize([running], midnight, new Date('2026-09-16T18:57:00Z'), undefined, noon);
    expect(s.breastfeed.total_s).toBe(120);
  });

  it('defaults the clock to the window end for Summary-style callers', () => {
    expect(summarize([running], midnight, noon).breastfeed.total_s).toBe(300);
  });
});

describe('day boundaries', () => {
  it('counts an entry logged at exactly midnight in one day only', () => {
    const d = mk('diaper', '2026-09-17T07:00:00Z', { wet: true, dirty: false, dry: false }); // 00:00 PDT
    const day16 = summarize([d], new Date('2026-09-16T07:00:00Z'), new Date('2026-09-17T07:00:00Z'));
    const day17 = summarize([d], new Date('2026-09-17T07:00:00Z'), new Date('2026-09-18T07:00:00Z'));
    expect([day16.diaper.count, day17.diaper.count]).toEqual([0, 1]);
    expect(groupByDay([d], 'America/Los_Angeles')[0].day).toBe('2026-09-17');
  });

  it('dayKeyIn agrees with localParts across a DST change', () => {
    for (const iso of ['2026-11-01T06:30:00Z', '2026-11-01T08:30:00Z', '2026-11-02T07:59:59Z', '2026-03-08T10:00:00Z']) {
      const at = new Date(iso);
      expect(dayKeyIn(at, 'America/Los_Angeles')).toBe(localParts(at, 'America/Los_Angeles').date);
    }
  });
});
