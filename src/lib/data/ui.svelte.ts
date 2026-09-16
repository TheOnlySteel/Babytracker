import type { Entry } from './types';

export type SheetKind = 'feed-picker' | 'bottle' | 'breastfeed' | 'diaper' | 'pump' | 'summary';
export interface SheetState { kind: SheetKind; entry?: Entry }

export const ui = $state<{ sheet: SheetState | null }>({ sheet: null });

export function openSheet(kind: SheetKind, entry?: Entry) {
  ui.sheet = { kind, entry };
}
export function closeSheet() {
  ui.sheet = null;
}
