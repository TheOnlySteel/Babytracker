/** Field validation shared by the sheets. Returns a message instead of throwing so forms can show it inline. */
export function nonNegative(v: number | null | undefined, label: string, opts: { max?: number } = {}): { value: number | null; error?: string } {
  if (v === null || v === undefined || (typeof v === 'string' && v === '')) return { value: null };
  const n = Number(v);
  if (!Number.isFinite(n)) return { value: null, error: `${label} must be a number` };
  if (n < 0) return { value: null, error: `${label} can't be negative` };
  if (opts.max !== undefined && n > opts.max) return { value: null, error: `${label} looks too large` };
  return { value: n };
}

export function notInFuture(d: Date, label = 'Time', slackMinutes = 10): string | undefined {
  if (d.getTime() > Date.now() + slackMinutes * 60_000) return `${label} is in the future`;
  return undefined;
}
