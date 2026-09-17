import type { Entry } from './types';

export type SheetKind = 'feed-picker' | 'bottle' | 'breastfeed' | 'diaper' | 'pump' | 'summary';
export interface SheetState { kind: SheetKind; entry?: Entry }

export const ui = $state<{ sheet: SheetState | null }>({ sheet: null });

/** The element that opened the first modal in a chain (e.g. the Feed + button), so focus can
 *  return to it after picker → form → close, even though the picker's own buttons are gone. */
let opener: Element | null = null;

export function openSheet(kind: SheetKind, entry?: Entry) {
  if (!ui.sheet && typeof document !== 'undefined') opener = document.activeElement;
  ui.sheet = { kind, entry };
}

export function closeSheet() {
  ui.sheet = null;
  const o = opener;
  opener = null;
  if (o instanceof HTMLElement && o.isConnected) {
    // After the modal unmounts, so the inert attribute is already gone.
    setTimeout(() => o.focus({ preventScroll: true }), 0);
  }
}
