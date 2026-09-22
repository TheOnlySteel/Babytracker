/** "2h 29m", "45m", "0m" */
export function fmtDuration(seconds: number): string {
  const m = Math.round(seconds / 60);
  const h = Math.floor(m / 60);
  const mm = m % 60;
  if (h === 0) return `${mm}m`;
  return `${h}h ${mm}m`;
}

/** "2h 29m ago", "just now" */
export function fmtAgo(iso: string, now = new Date()): string {
  const s = Math.max(0, (now.getTime() - new Date(iso).getTime()) / 1000);
  if (s < 60) return 'just now';
  return `${fmtDuration(s)} ago`;
}

/** "10:00am" */
export function fmtTime(iso: string | Date): string {
  const d = typeof iso === 'string' ? new Date(iso) : iso;
  let h = d.getHours();
  const m = String(d.getMinutes()).padStart(2, '0');
  const ap = h >= 12 ? 'pm' : 'am';
  h = h % 12 || 12;
  return `${h}:${m}${ap}`;
}

/** "Today 12:34pm", "Yesterday 9:10pm", "Sep 14 3:00pm" */
export function fmtWhen(iso: string | Date, now = new Date()): string {
  const d = typeof iso === 'string' ? new Date(iso) : iso;
  const dayDiff = Math.round((startOfDay(now).getTime() - startOfDay(d).getTime()) / 86400000);
  const t = fmtTime(d);
  if (dayDiff === 0) return `Today ${t}`;
  if (dayDiff === 1) return `Yesterday ${t}`;
  return `${fmtShortDate(d)} ${t}`;
}

/** "Wed, Sep 16" */
export function fmtHeaderDate(d: Date): string {
  return d.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' });
}

/** "Sep 16" */
export function fmtShortDate(d: Date): string {
  return d.toLocaleDateString('en-US', { month: 'short', day: 'numeric' });
}

/** "Wednesday, Sep 16" / "Today" / "Yesterday" */
export function fmtDayLabel(dayKey: string, now = new Date()): string {
  const [y, m, d] = dayKey.split('-').map(Number);
  const date = new Date(y, m - 1, d);
  const diff = Math.round((startOfDay(now).getTime() - date.getTime()) / 86400000);
  if (diff === 0) return 'Today';
  if (diff === 1) return 'Yesterday';
  return date.toLocaleDateString('en-US', { weekday: 'long', month: 'short', day: 'numeric' });
}

export function fmtKg(kg: number): string {
  const totalOz = kg / 0.45359237 * 16;
  const lb = Math.floor(totalOz / 16);
  const oz = totalOz - lb * 16;
  return `${lb} lb ${oz.toFixed(1)} oz`;
}

function startOfDay(d: Date): Date {
  const x = new Date(d);
  x.setHours(0, 0, 0, 0);
  return x;
}

/** Age in days from a YYYY-MM-DD birth date. */
export function ageDays(birthDate: string, now = new Date()): number {
  const [y, m, d] = birthDate.split('-').map(Number);
  // Round, not floor: after a spring-forward the span between two local midnights is 1 h short.
  return Math.round((startOfDay(now).getTime() - new Date(y, m - 1, d).getTime()) / 86400000);
}

/** "5w 3d" */
export function fmtAge(birthDate: string, now = new Date()): string {
  const days = ageDays(birthDate, now);
  const w = Math.floor(days / 7);
  const d = days % 7;
  return w === 0 ? `${d}d` : `${w}w ${d}d`;
}
