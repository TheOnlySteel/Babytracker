// Small pure helpers for describing entries in the UI.
import type { BottlePayload, BreastfeedPayload, DiaperPayload, Entry, PumpPayload, GrowthPayload } from './types';
import { bottleTotalMl, breastfeedTotalS, pumpTotalMl } from './types';
import { fmtDuration, fmtKg } from './format';

export function bottleKindLabel(p: BottlePayload): string {
  const has = (k: string) => p.kinds?.includes(k as never);
  if (has('breast_milk') && has('formula')) return 'Breast milk + formula';
  if (has('formula')) return 'Formula';
  if (has('breast_milk')) return 'Breast milk';
  return 'Bottle';
}

/** Short row label, e.g. "Formula", "Left · Right", "Wet", "Dirty, runny" */
export function entryLabel(e: Entry): string {
  switch (e.type) {
    case 'bottle':
      return bottleKindLabel(e.payload as BottlePayload);
    case 'breastfeed': {
      const p = e.payload as BreastfeedPayload;
      const sides = [p.left_s >= 30 && 'Left', p.right_s >= 30 && 'Right'].filter(Boolean);
      return sides.join(' · ') || 'Breastfeed';
    }
    case 'combo':
      return 'Combo';
    case 'diaper': {
      const p = e.payload as DiaperPayload;
      const kind = p.wet && p.dirty ? 'Wet + dirty' : p.dirty ? 'Dirty' : p.wet ? 'Wet' : 'Dry';
      const detail = [...(p.texture ?? []), ...(p.color ?? [])];
      const flags = [p.blowout && 'blowout', p.rash && 'rash'].filter(Boolean) as string[];
      const extra = [...detail, ...flags].join(', ');
      return extra ? `${kind} · ${extra}` : kind;
    }
    case 'pump': {
      const p = e.payload as PumpPayload;
      const parts = [p.left_ml != null && `${p.left_ml} L`, p.right_ml != null && `${p.right_ml} R`].filter(Boolean);
      return parts.join(' · ') || 'Pump';
    }
    case 'growth': {
      const p = e.payload as GrowthPayload;
      return [p.weight_kg != null && fmtKg(p.weight_kg), p.height_cm != null && `${p.height_cm} cm`, p.head_cm != null && `head ${p.head_cm} cm`].filter(Boolean).join(' · ');
    }
  }
}

/** Numeric magnitude used for the proportional bar, and its display string. */
export function entryMagnitude(e: Entry): { value: number; text: string } | null {
  switch (e.type) {
    case 'bottle': {
      const ml = bottleTotalMl(e.payload as BottlePayload);
      return { value: ml, text: `${ml} mL` };
    }
    case 'breastfeed': {
      const s = breastfeedTotalS(e.payload as BreastfeedPayload);
      return { value: s, text: fmtDuration(s) };
    }
    case 'combo': {
      const s = breastfeedTotalS(e.payload as BreastfeedPayload);
      const ml = bottleTotalMl(e.payload as BottlePayload);
      return { value: s, text: ml ? `${fmtDuration(s)} + ${ml} mL` : fmtDuration(s) };
    }
    case 'pump': {
      const ml = pumpTotalMl(e.payload as PumpPayload);
      return { value: ml, text: `${ml} mL` };
    }
    default:
      return null;
  }
}

/** Headline for the card's "last" row: { big: '45', unit: 'mL' } or { big: 'wet' } */
export function headline(e: Entry): { big: string; unit?: string } {
  switch (e.type) {
    case 'bottle':
      return { big: String(bottleTotalMl(e.payload as BottlePayload)), unit: 'mL' };
    case 'breastfeed':
    case 'combo':
      return { big: fmtDuration(breastfeedTotalS(e.payload as BreastfeedPayload)) };
    case 'diaper': {
      const p = e.payload as DiaperPayload;
      return { big: p.wet && p.dirty ? 'both' : p.dirty ? 'dirty' : p.wet ? 'wet' : 'dry' };
    }
    case 'pump':
      return { big: String(pumpTotalMl(e.payload as PumpPayload)), unit: 'mL' };
    case 'growth':
      return { big: '' };
  }
}

/** "Same again" description, e.g. "45 mL formula" / "Left, 20m" / "Wet" */
export function sameAgainLabel(e: Entry): string {
  switch (e.type) {
    case 'bottle': {
      const p = e.payload as BottlePayload;
      return `${bottleTotalMl(p)} mL ${bottleKindLabel(p).toLowerCase()}`;
    }
    case 'breastfeed': {
      const p = e.payload as BreastfeedPayload;
      const parts = [p.left_s >= 30 && `Left ${fmtDuration(p.left_s)}`, p.right_s >= 30 && `Right ${fmtDuration(p.right_s)}`].filter(Boolean);
      return parts.join(', ') || 'Breastfeed';
    }
    case 'diaper':
      return entryLabel(e);
    default:
      return entryLabel(e);
  }
}

/** Distinct recent bottle totals, most recent first. */
export function recentAmounts(entries: Entry[], n = 4): number[] {
  const out: number[] = [];
  for (const e of entries) {
    if (e.type !== 'bottle' || e.deleted_at) continue;
    const ml = bottleTotalMl(e.payload as BottlePayload);
    if (ml > 0 && !out.includes(ml)) out.push(ml);
    if (out.length >= n) break;
  }
  return out;
}

export function lastOf(entries: Entry[], pred: (e: Entry) => boolean): Entry | undefined {
  let best: Entry | undefined;
  for (const e of entries) {
    if (e.deleted_at || !pred(e)) continue;
    if (!best || e.started_at > best.started_at) best = e;
  }
  return best;
}

export function iconFor(e: Entry): 'bottle' | 'breast' | 'diaper' | 'pump' | 'growth' | 'combo' {
  return e.type === 'breastfeed' ? 'breast' : e.type;
}

export function runningElapsed(p: BreastfeedPayload, now: Date): { left_s: number; right_s: number; open: 'left' | 'right' | null } {
  let left = 0, right = 0, open: 'left' | 'right' | null = null;
  for (const seg of p.segments ?? []) {
    const end = seg.end ? new Date(seg.end).getTime() : now.getTime();
    const d = Math.max(0, (end - new Date(seg.start).getTime()) / 1000);
    if (seg.side === 'left') left += d; else right += d;
    if (!seg.end) open = seg.side;
  }
  return { left_s: Math.floor(left), right_s: Math.floor(right), open };
}
