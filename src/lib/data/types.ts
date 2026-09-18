export type EntryType =
  | 'breastfeed'
  | 'bottle'
  | 'combo'
  | 'diaper'
  | 'pump'
  | 'growth'
  | 'sleep';
export type Side = 'left' | 'right';
export type BottleKind = 'breast_milk' | 'formula';
export type Texture = 'runny' | 'mucousy' | 'mushy' | 'solid' | 'pebbles';
export type DiaperColor =
  | 'black'
  | 'green'
  | 'yellow'
  | 'brown'
  | 'red'
  | 'gray';

export interface Segment {
  side: Side;
  start: string;
  end: string | null;
}

export interface BreastfeedPayload {
  begin_side: Side | null;
  end_side: Side | null;
  left_s: number;
  right_s: number;
  manual: boolean;
  segments: Segment[];
}
export interface BottlePayload {
  kinds: BottleKind[];
  breast_milk_ml?: number;
  formula_ml?: number;
  formula_brand?: string;
}
export type ComboPayload = BreastfeedPayload & BottlePayload;
export interface DiaperPayload {
  wet: boolean;
  dirty: boolean;
  dry: boolean;
  texture: Texture[];
  color: DiaperColor[];
  blowout: boolean;
  rash: boolean;
}
export type SleepKind = 'nap' | 'night';
export interface SleepPayload {
  kind: SleepKind;
  source: 'manual' | 'cradlewise';
  place?: 'crib' | 'stroller' | 'car' | 'arms' | 'other';
  provisional?: boolean;
  timing_locked?: boolean;
  uncertain_end?: boolean;
  night_key?: string;
  suppressed_source_key?: string;
}
export interface SleepStatus {
  household_id: string;
  status: string;
  since: string | null;
  observed_at: string | null;
  source_error: string | null;
  history_error?: string | null;
  bounce: string | null;
  music: string | null;
  derive_enabled: boolean;
  bed_min: number | null;
  rise_min: number | null;
  timezone?: string;
  requests_24h?: number;
}
export interface Device {
  id: string;
  name: string;
  boot_mode: 'hub' | 'dashboard' | 'lamp';
  last_seen_at: string | null;
  revoked_at: string | null;
}
export interface PumpPayload {
  total_ml?: number;
  left_ml?: number;
  right_ml?: number;
}
export interface GrowthPayload {
  weight_kg?: number;
  height_cm?: number;
  head_cm?: number;
}

export type Payload =
  | BreastfeedPayload
  | BottlePayload
  | ComboPayload
  | DiaperPayload
  | PumpPayload
  | GrowthPayload
  | SleepPayload;

export interface Entry<P = Payload> {
  id: string;
  household_id: string;
  child_id: string | null;
  type: EntryType;
  started_at: string;
  ended_at: string | null;
  payload: P;
  note: string | null;
  created_by: string | null;
  updated_by: string | null;
  created_at: string;
  updated_at: string;
  deleted_at: string | null;
  nara_activity_key: string | null;
  source_key?: string | null;
  via?: string;
}

export interface Caregiver {
  user_id: string;
  household_id: string;
  display_name: string;
}
export interface Child {
  id: string;
  household_id: string;
  name: string;
  sex: string | null;
  birth_date: string;
}
export interface Prefs {
  formula_brands?: string[];
  last_formula_brand?: string;
  last_texture?: Texture[];
  last_color?: DiaperColor[];
}

export const TEXTURES: Texture[] = [
  'runny',
  'mucousy',
  'mushy',
  'solid',
  'pebbles',
];
export const DIAPER_COLORS: DiaperColor[] = [
  'black',
  'green',
  'yellow',
  'brown',
  'red',
  'gray',
];
export const DIAPER_SWATCH: Record<DiaperColor, string> = {
  black: '#3b2412',
  green: '#7a9b34',
  yellow: '#e2b93b',
  brown: '#7b4a1e',
  red: '#d9442f',
  gray: '#d6d6d6',
};

export const isFeed = (t: EntryType) =>
  t === 'breastfeed' || t === 'bottle' || t === 'combo';
export const bottleTotalMl = (p: BottlePayload) =>
  (p.breast_milk_ml ?? 0) + (p.formula_ml ?? 0);
export const pumpTotalMl = (p: PumpPayload) =>
  p.total_ml ?? (p.left_ml ?? 0) + (p.right_ml ?? 0);
export const breastfeedTotalS = (p: BreastfeedPayload) =>
  (p.left_s ?? 0) + (p.right_s ?? 0);

export const isSleep = (e: Entry): e is Entry<SleepPayload> =>
  e.type === 'sleep';
