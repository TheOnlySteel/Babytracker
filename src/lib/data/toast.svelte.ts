export interface Toast {
  id: number;
  message: string;
  undo?: () => void | Promise<void>;
  /** milliseconds; 0 keeps the toast until dismissed */
  ttl: number;
  kind: 'info' | 'error';
}

/** Undo windows: long enough to notice while holding a baby. */
export const UNDO_TTL = 8000;

let seq = 0;
export const toasts = $state<{ list: Toast[] }>({ list: [] });

export function toast(message: string, opts: { undo?: () => void | Promise<void>; ttl?: number; kind?: 'info' | 'error' } = {}) {
  const t: Toast = { id: ++seq, message, undo: opts.undo, ttl: opts.ttl ?? (opts.undo ? UNDO_TTL : 2500), kind: opts.kind ?? 'info' };
  toasts.list = [...toasts.list, t];
  if (t.ttl > 0) setTimeout(() => dismiss(t.id), t.ttl);
  return t.id;
}

/** Errors stay until dismissed: a 2.5 s flash is not enough to read while tending to a baby. */
export function toastError(message: string) {
  // Collapse repeats of the same error.
  if (toasts.list.some((t) => t.kind === 'error' && t.message === message)) return;
  return toast(message, { ttl: 0, kind: 'error' });
}

export function dismiss(id: number) {
  toasts.list = toasts.list.filter((t) => t.id !== id);
}
