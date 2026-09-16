export interface Toast {
  id: number;
  message: string;
  undo?: () => void | Promise<void>;
  ttl: number;
}

let seq = 0;
export const toasts = $state<{ list: Toast[] }>({ list: [] });

export function toast(message: string, opts: { undo?: () => void | Promise<void>; ttl?: number } = {}) {
  const t: Toast = { id: ++seq, message, undo: opts.undo, ttl: opts.ttl ?? (opts.undo ? 4000 : 2500) };
  toasts.list = [...toasts.list, t];
  setTimeout(() => dismiss(t.id), t.ttl);
  return t.id;
}

export function dismiss(id: number) {
  toasts.list = toasts.list.filter((t) => t.id !== id);
}
