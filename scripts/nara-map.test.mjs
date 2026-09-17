import { describe, it, expect } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import Papa from 'papaparse';
import { mapRow } from './nara-map.mjs';
import { summarize } from '../src/lib/data/summary.ts';

const ctx = {
  household_id: 'hh',
  child_id: 'child',
  caregivers: { Steel: 'u-steel', Dominique: 'u-dom' }
};

const base = {
  Type: '',
  'Start Date/time (Epoch)': '1789578053000',
  'Created By Caregiver': 'Steel',
  'Last Updated By Caregiver': 'Dominique',
  Note: '',
  _activityKey: 't-1'
};

describe('mapRow', () => {
  it('maps a formula bottle', () => {
    const e = mapRow({ ...base, Type: 'Bottle Feed', '[Bottle Feed] Type': 'Formula', '[Bottle Feed] Formula Name': 'Good Start Plus', '[Bottle Feed] Formula Volume': '45', '[Bottle Feed] Formula Volume Unit': 'ML' }, ctx);
    expect(e.type).toBe('bottle');
    expect(e.child_id).toBe('child');
    expect(e.payload).toEqual({ kinds: ['formula'], formula_ml: 45, formula_brand: 'Good Start Plus' });
    expect(e.created_by).toBe('u-steel');
    expect(e.updated_by).toBe('u-dom');
    expect(e.started_at).toBe('2026-09-16T17:00:53.000Z');
    expect(e.ended_at).toBeNull();
  });

  it('maps a mixed bottle', () => {
    const e = mapRow({ ...base, Type: 'Bottle Feed', '[Bottle Feed] Type': 'Breast Milk Formula', '[Bottle Feed] Breast Milk Volume': '30', '[Bottle Feed] Breast Milk Volume Unit': 'ML', '[Bottle Feed] Formula Volume': '40', '[Bottle Feed] Formula Volume Unit': 'ML', '[Bottle Feed] Formula Name': 'Enfamil Neuropro' }, ctx);
    expect(e.payload.kinds).toEqual(['breast_milk', 'formula']);
    expect(e.payload.breast_milk_ml).toBe(30);
    expect(e.payload.formula_ml).toBe(40);
  });

  it('rejects non-mL units', () => {
    expect(() => mapRow({ ...base, Type: 'Bottle Feed', '[Bottle Feed] Type': 'Formula', '[Bottle Feed] Formula Volume': '2', '[Bottle Feed] Formula Volume Unit': 'OZ' }, ctx)).toThrow(/Unexpected unit/);
  });

  it('maps a breastfeed with .nonTimer as manual and derives ended_at', () => {
    const e = mapRow({ ...base, Type: 'Breastfeed', '[Breastfeed] Begin Side': 'LEFT', '[Breastfeed] End Side': 'LEFT.nonTimer', '[Breastfeed] Left Duration (Seconds)': '1200' }, ctx);
    expect(e.payload).toEqual({ begin_side: 'left', end_side: 'left', left_s: 1200, right_s: 0, manual: true, segments: [] });
    expect(e.ended_at).toBe('2026-09-16T17:20:53.000Z');
  });

  it('maps a dirty+wet diaper with multi-token colour and texture', () => {
    const e = mapRow({ ...base, Type: 'Diaper', '[Diaper] Type': 'Dirty Wet', '[Diaper] Detail': 'Blowout', '[Diaper] Dirty Color': 'BROWN YELLOW', '[Diaper] Dirty Texture': 'MUSH RUN' }, ctx);
    expect(e.payload).toEqual({ wet: true, dirty: true, dry: false, texture: ['mushy', 'runny'], color: ['brown', 'yellow'], blowout: true, rash: false });
  });

  it('maps a pump with null child and real end time', () => {
    const e = mapRow({ ...base, Type: 'Pump', '[Pump] End Date/time (Epoch)': '1789579433000', '[Pump] Left Volume': '70', '[Pump] Left Volume Unit': 'ML', '[Pump] Right Volume': '60', '[Pump] Right Volume Unit': 'ML' }, ctx);
    expect(e.child_id).toBeNull();
    expect(e.payload).toEqual({ left_ml: 70, right_ml: 60 });
    expect(e.ended_at).toBe('2026-09-16T17:23:53.000Z');
  });

  it('converts growth LB to kg', () => {
    const e = mapRow({ ...base, Type: 'Growth', '[Growth] Weight': '9.80625', '[Growth] Weight Unit': 'LB' }, ctx);
    expect(e.payload.weight_kg).toBeCloseTo(4.448, 3);
  });

  it('skips out-of-scope types', () => {
    expect(mapRow({ ...base, Type: 'Milestone' }, ctx)).toBeNull();
    expect(mapRow({ ...base, Type: 'Profile' }, ctx)).toBeNull();
  });
});

describe('mapRow validation', () => {
  it('rejects a row with no activity key', () => {
    expect(() => mapRow({ ...base, _activityKey: '', Type: 'Diaper', '[Diaper] Type': 'Wet' }, ctx)).toThrow(/activity key/);
  });
  it('rejects a non-numeric volume instead of storing NaN', () => {
    expect(() => mapRow({ ...base, Type: 'Bottle Feed', '[Bottle Feed] Type': 'Formula', '[Bottle Feed] Formula Volume': 'lots', '[Bottle Feed] Formula Volume Unit': 'ML' }, ctx)).toThrow(/not a number/);
  });
  it('rejects a mixed bottle with only a generic total', () => {
    expect(() => mapRow({ ...base, Type: 'Bottle Feed', '[Bottle Feed] Type': 'Breast Milk Formula', '[Bottle Feed] Volume': '60', '[Bottle Feed] Volume Unit': 'ML' }, ctx)).toThrow(/split/);
  });
});

// Acceptance test from spec §4 against the real export (gitignored). The whole block is
// conditional so a clean checkout never touches the file during collection.
const EXPORT = new URL('../data/export_narababy_rosalie_20260916.csv', import.meta.url);
const realExport = () => {
  const csv = readFileSync(EXPORT, 'utf8').replace(/^\uFEFF/, '');
  const { data: rows } = Papa.parse(csv, { header: true, skipEmptyLines: true });
  return rows.map((r) => mapRow(r, ctx)).filter(Boolean).map((e, i) => ({ ...e, id: String(i), created_at: '', updated_at: '', deleted_at: null }));
};
describe.skipIf(!existsSync(EXPORT))('real export acceptance (spec §4)', () => {
  const entries = existsSync(EXPORT) ? realExport() : [];

  it('maps every in-scope row', () => {
    const counts = entries.reduce((a, e) => ((a[e.type] = (a[e.type] ?? 0) + 1), a), {});
    expect(counts).toEqual({ breastfeed: 347, diaper: 271, bottle: 119, pump: 70, growth: 12, combo: 2 });
    expect(new Set(entries.map((e) => e.nara_activity_key)).size).toBe(entries.length);
  });

  // 2026-09-16 12:30 America/Vancouver (PDT, UTC-7) = 19:30Z
  const now = new Date('2026-09-16T19:30:00Z');
  const midnight = new Date('2026-09-16T07:00:00Z');

  it('Today matches IMG_2597', () => {
    const s = summarize(entries, midnight, now, '2026-08-09');
    expect(s.breastfeed).toEqual({ count: 2, total_s: 2400, left_s: 1200, right_s: 1200 });
    expect(s.bottle).toEqual({ count: 5, total_ml: 375, breast_milk_ml: 0, formula_ml: 375 });
    expect(s.diaper).toMatchObject({ count: 7, wet: 7, dirty: 3 });
    expect(s.pump).toEqual({ count: 1, total_ml: 130, left_ml: 70, right_ml: 60 });
  });

  it('Last 24h matches IMG_2596', () => {
    const s = summarize(entries, new Date(now.getTime() - 24 * 3600 * 1000), now, '2026-08-09');
    expect(s.breastfeed.count).toBe(9);
    expect(Math.round(s.breastfeed.total_s / 60)).toBe(158); // 2h 38m
    expect(Math.round(s.breastfeed.left_s / 60)).toBe(82); // 1h 22m
    expect(Math.round(s.breastfeed.right_s / 60)).toBe(76); // 1h 16m
    expect(s.bottle).toEqual({ count: 8, total_ml: 715, breast_milk_ml: 90, formula_ml: 625 });
    expect(s.diaper).toMatchObject({ count: 13, wet: 13, dirty: 5 });
    expect(s.pump).toEqual({ count: 1, total_ml: 130, left_ml: 70, right_ml: 60 });
  });
});
