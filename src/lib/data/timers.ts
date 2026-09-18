import type { BreastfeedPayload, Entry, SleepPayload } from "./types";
import { runningElapsed } from "./derive";
/** Shared phone stop semantics. Never use a default duration to stop an actual timer. */
export function stopPatch(e: Entry, now: Date) {
  const end = new Date(Math.max(+now, Date.parse(e.started_at))).toISOString();
  let payload = { ...e.payload };
  if (e.type === "breastfeed") {
    const p = e.payload as BreastfeedPayload;
    const segments = p.segments.map((s) => (s.end ? s : { ...s, end }));
    const totals = runningElapsed({ ...p, segments }, new Date(end));
    payload = {
      ...p,
      segments,
      left_s: totals.left_s,
      right_s: totals.right_s,
      end_side: segments.at(-1)?.side ?? p.begin_side,
      manual: false,
    };
  } else if (e.type === "sleep")
    payload = { ...(e.payload as SleepPayload), timing_locked: true };
  return { ended_at: end, payload };
}
