import { sleepKind, sourceKey } from '../_shared/sleep-time.ts';
export interface Observation {
  status: string;
  since: string | null;
  observed_at: string;
}
export interface SleepRow {
  id: string;
  started_at: string;
  ended_at: string | null;
  updated_at: string;
  deleted_at: string | null;
  source_key: string | null;
  payload: Record<string, unknown>;
}
export interface Action {
  op: 'insert' | 'update';
  id?: string;
  version?: string;
  source_key?: string;
  started_at?: string;
  ended_at?: string | null;
  payload?: Record<string, unknown>;
}
export interface Context {
  now: string;
  timezone: string;
  bed_min?: number | null;
  rise_min?: number | null;
  /** Latest observed crib status. Reconciliation never ends an open row while it is sleeping or stirring. */
  status?: string | null;
}
const ms = (v: string) => Date.parse(v);
const NON_SLEEP = new Set(['awake', 'crying', 'away']);
const GAP_MS = 240000; // more than one missed 30/60 s poll: the stream is not continuous
const CONFIRM_MS = 300000; // five minutes of sleep opens, five minutes of not-sleep closes
const PICKUP_MS = 1200000; // away after twenty minutes asleep is a pickup, not a blip
const update = (e: SleepRow, fields: Partial<Action>): Action => ({ op: 'update', id: e.id, version: e.updated_at, ...fields });

/** One pass over a continuous run of observations: the sleep episode that is open and the
 *  non-sleep episode that is open, if any. Awake, crying and away form ONE episode; a status
 *  label change inside it does not restart the debounce. Stirring continues either side. */
function scan(obs: Observation[]) {
  let asleep: Observation | undefined, awake: Observation | undefined, previous: Observation | undefined;
  for (const o of obs) {
    if (previous && ms(o.observed_at) - ms(previous.observed_at) > GAP_MS) {
      asleep = undefined;
      awake = undefined;
    }
    if (o.status === 'sleeping') {
      // A changed upstream `since` marks a new episode, including after a missed wake.
      if (!asleep || (o.since && previous?.status === 'sleeping' && previous.since !== o.since)) asleep = o;
      awake = undefined;
    } else if (o.status === 'stirring') {
      // continue an established sleep, but stirring alone cannot open one
    } else if (NON_SLEEP.has(o.status)) {
      asleep = undefined;
      awake ??= o;
    } else {
      asleep = undefined;
      awake = undefined;
    }
    previous = o;
  }
  return { asleep, awake };
}

/** The close action for an open, unlocked row given the non-sleep episode and the last reliable observation. */
function closeAt(open: SleepRow, awake: Observation, last: Observation): Action | null {
  const end = Math.max(ms(open.started_at), Math.min(ms(awake.since ?? awake.observed_at), ms(awake.observed_at)));
  const confirmed = ms(last.observed_at) - ms(awake.observed_at) >= CONFIRM_MS;
  const pickup = last.status === 'away' && ms(last.observed_at) - ms(open.started_at) >= PICKUP_MS;
  if (!confirmed && !pickup) return null;
  return update(open, { ended_at: new Date(end).toISOString(), payload: { uncertain_end: false } });
}

/** Decisions only; the database fences the lease, checks versions, and applies atomically. */
export function deriveSleep(observations: Observation[], entries: SleepRow[], ctx: Context): Action[] {
  const obs = observations.filter((o) => ms(o.observed_at) <= ms(ctx.now)).sort((a, b) => ms(a.observed_at) - ms(b.observed_at));
  if (!obs.length) return [];
  const open = entries.find((e) => !e.deleted_at && !e.ended_at);
  const unlocked = open && !open.payload.timing_locked && (open.payload.source === 'cradlewise' || open.source_key);
  // A dead poller must close at the last reliable observation, even without a new success.
  // A wake that was already confirmed before the gap wins over the gap rule.
  if (unlocked) {
    const during = obs.filter((o) => ms(o.observed_at) >= ms(open.started_at));
    for (let i = 0; i < during.length; i++) {
      const next = during[i + 1]?.observed_at ?? ctx.now;
      if (ms(next) - ms(during[i].observed_at) > GAP_MS) {
        const before = scan(during.slice(0, i + 1));
        const wake = before.awake && closeAt(open, before.awake, during[i]);
        return [wake ?? update(open, { ended_at: during[i].observed_at, payload: { uncertain_end: true } })];
      }
    }
  }
  const { asleep, awake } = scan(obs);
  const last = obs.at(-1)!;
  if (ms(ctx.now) - ms(last.observed_at) > GAP_MS) return [];
  if (unlocked && awake) {
    const action = closeAt(open, awake, last);
    if (action) return [action];
  }
  if (!asleep || ms(last.observed_at) - ms(asleep.observed_at) < CONFIRM_MS) return [];
  const start = asleep.since ?? asleep.observed_at;
  const key = sourceKey(start);
  // A wake shorter than one poll interval shows up only as a changed `since`. Close the
  // previous crib episode at the new start so the next tick can open the new one.
  if (unlocked && open.source_key && open.source_key !== key && open.payload.source === 'cradlewise' && ms(start) > ms(open.started_at))
    return [update(open, { ended_at: start, payload: { uncertain_end: true } })];
  // A completed manual interval overlapping this same crib episode is already a
  // human account of it. Do not reopen the episode after the manual timer ended.
  if (entries.some((e) => !e.deleted_at && e.ended_at && e.payload.source === 'manual' && ms(e.started_at) < ms(last.observed_at) && ms(e.ended_at) > ms(start)))
    return [];
  if (entries.some((e) => e.source_key === key || e.payload.suppressed_source_key === key)) return [];
  if (open) {
    if (open.payload.source === 'manual' && !open.source_key && !open.payload.timing_locked) {
      return [update(open, open.payload.place === 'crib' ? { source_key: key } : { payload: { suppressed_source_key: key } })];
    }
    return [];
  }
  return [
    {
      op: 'insert',
      source_key: key,
      started_at: start,
      ended_at: null,
      payload: { source: 'cradlewise', provisional: true, place: 'crib', ...sleepKind(start, ctx.timezone, ctx.bed_min ?? 1200, ctx.rise_min ?? 480) }
    }
  ];
}
