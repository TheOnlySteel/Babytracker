/**
 * Calendar arithmetic in the household's IANA zone. Shared by the web app and the poller,
 * so it must stay free of Deno, SvelteKit and Node imports.
 */
export function localParts(at: Date, tz: string) {
  const parts = Object.fromEntries(
    new Intl.DateTimeFormat('en-CA', {
      timeZone: tz,
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hourCycle: 'h23'
    })
      .formatToParts(at)
      .map((p) => [p.type, p.value])
  );
  return {
    date: `${parts.year}-${parts.month}-${parts.day}`,
    minute: Number(parts.hour) * 60 + Number(parts.minute),
    text: `${parts.year}-${parts.month}-${parts.day} ${parts.hour}:${parts.minute}:${parts.second}`
  };
}

/** True when Intl knows the zone. The database validates against pg_timezone_names, but a
 *  browser or Deno build may lag; callers fall back to UTC rather than crash. */
export function validTimezone(tz: unknown): tz is string {
  if (typeof tz !== 'string' || !tz) return false;
  try {
    new Intl.DateTimeFormat('en-CA', { timeZone: tz });
    return true;
  } catch {
    return false;
  }
}

export function shiftDate(date: string, days: number) {
  return new Date(Date.parse(`${date}T12:00:00Z`) + days * 86400000).toISOString().slice(0, 10);
}

/**
 * Convert a local wall-clock string to an instant.
 *
 * `strict` (vendor timestamps): a repeated or skipped DST hour throws, so a source event is
 * never placed on a guess. `lenient` (our own calendar boundaries such as midnight or a rise
 * time): a repeated hour resolves to its first occurrence and a skipped hour to the first valid
 * instant after the gap, so a day boundary always exists.
 */
export function localInstant(value: string, tz: string, mode: 'strict' | 'lenient' = 'lenient'): Date {
  if (/Z$|[+-]\d\d:\d\d$/.test(value)) {
    const d = new Date(value);
    if (!Number.isFinite(+d)) throw new Error('Invalid timestamp');
    return d;
  }
  const match = value.match(/^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2}:\d{2})(?:\.\d+)?$/);
  if (!match) throw new Error('Invalid local timestamp');
  const text = `${match[1]} ${match[2]}`;
  const nominal = Date.parse(text.replace(' ', 'T') + 'Z');
  const offsets = new Set(
    [-36, -12, 0, 12, 36].map((h) => {
      const probe = new Date(nominal + h * 3600000);
      return Date.parse(localParts(probe, tz).text.replace(' ', 'T') + 'Z') - +probe;
    })
  );
  const candidates = [...offsets].map((offset) => new Date(nominal - offset)).sort((a, b) => +a - +b);
  const matches = candidates.filter((d) => localParts(d, tz).text === text);
  if (matches.length === 1) return matches[0];
  if (mode === 'strict') throw new Error('Ambiguous or nonexistent local timestamp');
  if (matches.length > 1) return matches[0]; // first occurrence of a repeated hour
  // Skipped hour: the earliest candidate that lands after the requested wall-clock time.
  const after = candidates.find((d) => localParts(d, tz).text > text);
  return after ?? candidates[candidates.length - 1];
}

export function sleepKind(at: string, tz: string, bed = 1200, rise = 480): { kind: 'nap' | 'night'; night_key?: string } {
  const p = localParts(new Date(at), tz),
    begin = (bed - 60 + 1440) % 1440;
  const night = begin > rise ? p.minute >= begin || p.minute < rise : p.minute >= begin && p.minute < rise;
  return night
    ? { kind: 'night', night_key: begin > rise && p.minute < rise ? shiftDate(p.date, -1) : p.date }
    : { kind: 'nap' };
}

export const sourceKey = (since: string) => 'cw:' + new Date(since).toISOString().replace(/\.\d{3}Z$/, 'Z');

/** Local midnight of the calendar day containing `now`, in the household zone. Never throws. */
export function householdDay(now: Date, tz: string) {
  return localInstant(`${localParts(now, tz).date} 00:00:00`, tz, 'lenient');
}
