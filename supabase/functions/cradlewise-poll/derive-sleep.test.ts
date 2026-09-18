import { describe, it, expect } from "vitest";
import { deriveSleep, type Observation, type SleepRow } from "./derive-sleep";
import {
  parseStatus,
  parseHistory,
  parseMetrics,
  reconcileSleep,
} from "./cradlewise";
const t = (min: number) =>
  new Date(Date.UTC(2026, 8, 17, 17, min)).toISOString();
const obs = (min: number, status = "sleeping", since = 0): Observation => ({
  observed_at: t(min),
  status,
  since: t(since),
});
const ctx = (min = 30) => ({ now: t(min), timezone: "America/Los_Angeles" });
const row = (patch: Partial<SleepRow> = {}): SleepRow => ({
  id: "s",
  started_at: t(0),
  ended_at: null,
  updated_at: t(0),
  deleted_at: null,
  source_key: "cw:" + t(0).replace(".000", ""),
  payload: { source: "cradlewise", kind: "nap", provisional: true },
  ...patch,
});
const seq = (n: number, status = "sleeping") =>
  Array.from({ length: n + 1 }, (_, i) => obs(i, status));
describe("live sleep derivation", () => {
  it("requires five observed minutes, not just a backdated since", () => {
    expect(deriveSleep([obs(10)], [], ctx(10))).toEqual([]);
    expect(deriveSleep(seq(5), [], ctx(5))[0]).toMatchObject({
      op: "insert",
      started_at: t(0),
      payload: { provisional: true, place: "crib" },
    });
  });
  it("rejects a false start and continues through stirring", () => {
    expect(deriveSleep([...seq(3), obs(4, "crying", 4)], [], ctx(4))).toEqual(
      [],
    );
    expect(
      deriveSleep(
        [...seq(2), obs(3, "stirring", 3), obs(4, "stirring", 3), obs(5)],
        [],
        ctx(5),
      )[0].op,
    ).toBe("insert");
  });
  it("debounces awake and crying together", () => {
    expect(
      deriveSleep(
        [
          ...seq(20),
          obs(21, "awake", 21),
          obs(23, "crying", 23),
          obs(26, "crying", 23),
        ],
        [row()],
        ctx(26),
      )[0],
    ).toMatchObject({ ended_at: t(21) });
  });
  it("closes immediately on pickup after twenty minutes", () => {
    expect(
      deriveSleep([...seq(24), obs(25, "away", 25)], [row()], ctx(25))[0]
        .ended_at,
    ).toBe(t(25));
  });
  it("closes a gap at the last reliable observation, including a dead feed", () => {
    for (const input of [seq(10), [...seq(10), obs(20)]])
      expect(deriveSleep(input, [row()], ctx(20))[0]).toMatchObject({
        ended_at: t(10),
        payload: { uncertain_end: true },
      });
  });
  it("does not bridge a gap before opening", () => {
    expect(deriveSleep([obs(0), obs(10)], [], ctx(10))).toEqual([]);
  });
  it("adopts a crib timer and suppresses a stroller episode", () => {
    const manual = row({
      source_key: null,
      payload: { source: "manual", place: "crib" },
    });
    expect(deriveSleep(seq(5), [manual], ctx(5))[0]).toMatchObject({
      id: "s",
      source_key: row().source_key,
    });
    manual.payload.place = "stroller";
    expect(
      deriveSleep(seq(5), [manual], ctx(5))[0].payload?.suppressed_source_key,
    ).toBe(row().source_key);
  });
  it("consumes human endings and dismissals; annotations do not own timing", () => {
    expect(
      deriveSleep(
        seq(5),
        [
          row({
            ended_at: t(3),
            payload: { source: "cradlewise", timing_locked: true },
          }),
        ],
        ctx(5),
      ),
    ).toEqual([]);
    expect(deriveSleep(seq(5), [row({ deleted_at: t(2) })], ctx(5))).toEqual(
      [],
    );
    expect(
      deriveSleep(
        [...seq(24), obs(25, "away", 25)],
        [row({ payload: { source: "cradlewise", place: "arms" } })],
        ctx(25),
      ),
    ).toHaveLength(1);
  });
  it("restarts safely with an existing timer", () => {
    expect(deriveSleep(seq(30), [row()], ctx(30))).toEqual([]);
  });
  it("closes the old episode when a wake shorter than one poll only shows as a changed since", () => {
    const actions = deriveSleep(
      [...seq(20), obs(21, "sleeping", 21), obs(22, "sleeping", 21), obs(23, "sleeping", 21), obs(24, "sleeping", 21), obs(25, "sleeping", 21), obs(26, "sleeping", 21)],
      [row()],
      ctx(26),
    );
    expect(actions).toEqual([expect.objectContaining({ op: "update", id: "s", ended_at: t(21), payload: { uncertain_end: true } })]);
  });
  it("prefers a wake confirmed before an outage over the gap rule", () => {
    // Asleep, then awake from minute 20; poller dies at minute 26 and comes back at 40.
    const actions = deriveSleep([...seq(19), obs(20, "awake", 20), obs(23, "awake", 20), obs(26, "awake", 20), obs(40, "awake", 20)], [row()], ctx(40));
    expect(actions).toEqual([expect.objectContaining({ ended_at: t(20), payload: { uncertain_end: false } })]);
  });
  it("continues through two stirrings inside one nap", () => {
    const actions = deriveSleep([...seq(10), obs(11, "stirring", 11), obs(12), obs(13, "stirring", 13), obs(14), obs(15)], [row()], ctx(15));
    expect(actions).toEqual([]);
  });
  it("does not close on a short away", () => {
    expect(deriveSleep([...seq(10), obs(11, "away", 11), obs(12, "away", 11), obs(13)], [row()], ctx(13))).toEqual([]);
  });
});
describe("upstream contracts (synthetic, not captured baby data)", () => {
  it("keeps a cached response old and rejects invalid timestamps/statuses", () => {
    const raw = {
      status: "sleeping",
      since: t(0),
      timestamp: t(5),
      crib_mode: { bounce: "on", music: "off" },
    };
    expect(parseStatus(raw, t(20)).observed_at).toBe(t(5));
    // The docs promise new values/fields may appear: an undocumented status is recorded as
    // unknown, a missing crib_mode/timestamp degrades to defaults, and only a missing/future
    // `since` rejects the poll.
    expect(parseStatus({ ...raw, status: "maybe" }, t(20)).status).toBe("unknown");
    const { crib_mode: _m, timestamp: _t, ...bare } = raw;
    expect(parseStatus(bare, t(20))).toMatchObject({ status: "sleeping", bounce: null, observed_at: t(20) });
    expect(() => parseStatus({ ...raw, since: t(30) }, t(20))).toThrow();
    expect(() => parseStatus({ ...raw, since: undefined }, t(20))).toThrow();
  });
  it("reads bed and rise times whichever way the raw value is expressed", () => {
    // The first live response (2026-09-18): raw values are offsetless UTC, display strings local.
    const live = parseMetrics(
      {
        timezone: "America/Vancouver",
        metrics: [
          {
            date: "2026-09-17 20:48:41.000000",
            banners: [
              { header: "RISE TIME", data: { value: "2026-09-17 19:02:07.352690", display_value: "12:02 pm" } },
              { header: "BEDTIME", data: { value: "2026-09-18 05:58:26.573776", display_value: "10:58 pm" } },
              { header: "NAPS", data: { value: 1, naps: [{ title: "Nap 1", start_time: "2:59 pm", end_time: "3:28 pm", duration: "28m" }] } },
              { header: "AWAKE IN BED", data: { value: 28, display_value: "28m" } },
            ],
          },
        ],
      },
      "America/Los_Angeles",
    );
    expect(live).toMatchObject({ bed_min: 22 * 60 + 58, rise_min: 12 * 60 + 2, day_metrics: { awake_in_bed_s: 28, naps: [] } });
    // The documented form (local raw values) still reads correctly because the display string decides.
    const documented = parseMetrics(
      {
        timezone: "America/Los_Angeles",
        metrics: [
          {
            date: "2026-09-17 08:00:00.000000",
            banners: [
              { header: "BEDTIME", data: { value: "2026-09-16 20:30:00.000000", display_value: "8:30 pm" } },
              { header: "RISE TIME", data: { value: "2026-09-17 07:45:00.000000", display_value: "7:45 am" } },
            ],
          },
        ],
      },
      "America/Los_Angeles",
    );
    expect(documented).toMatchObject({ bed_min: 1230, rise_min: 465 });
  });
  it("extracts explicit sleep boundaries and drops only the intervals it cannot trust", () => {
    const raw = {
      timezone: "America/Los_Angeles",
      events: [
        { event_name: "deep_sleep", event_label: "sleep", event_time: "2026-09-17 17:00:00.000000", is_user_added: false },
        { event_label: "awake", event_time: "2026-09-17 18:00:00.000000", is_user_added: false },
      ],
    };
    expect(parseHistory(raw, raw.timezone)).toEqual({ sleeps: [{ start: t(0), end: t(60) }], unknownLabels: [], droppedIntervals: 0 });
    // An unknown label inside a sleep taints that interval only; a later clean interval survives,
    // and the label is reported so Settings can show it.
    const mixed = parseHistory(
      {
        ...raw,
        events: [
          ...raw.events.slice(0, 1),
          { event_label: "not-in-crib", event_time: "2026-09-17 17:30:00" },
          ...raw.events.slice(1),
          { event_label: "sleep", event_time: "2026-09-17 19:00:00" },
          { event_label: "awake", event_time: "2026-09-17 19:30:00" },
        ],
      },
      raw.timezone,
    );
    expect(mixed).toEqual({ sleeps: [{ start: t(120), end: t(150) }], unknownLabels: ["not-in-crib"], droppedIntervals: 1 });
    // A caregiver-added event is a human account: that interval is left alone, the batch is not.
    const user = parseHistory(
      { ...raw, events: [{ ...raw.events[0], is_user_added: true }, raw.events[1], { event_label: "sleep", event_time: "2026-09-17 19:00:00" }, { event_label: "awake", event_time: "2026-09-17 19:30:00" }] },
      raw.timezone,
    );
    expect(user.sleeps).toEqual([{ start: t(120), end: t(150) }]);
    // Event times are UTC whatever zone the response or the household names: the first live
    // response's last event equalled the status endpoint's UTC `since` to the microsecond.
    expect(parseHistory({ ...raw, timezone: "America/New_York" }, "America/Los_Angeles").sleeps[0].start).toBe(t(0));
  });
  it("reads the live c-chart shape: UTC event times, the full label vocabulary, nothing unknown", () => {
    const live = {
      timezone: "America/Vancouver",
      day_start_time: "2026-09-18 08:00:00.000000",
      sessions: [{ session_id: "s", start_time: "2026-09-18 05:58:26.573776", end_time: "2026-09-18 11:22:40.101291", is_user_added: false, user_added_sleep_id: null }],
      events: [
        { event_name: "deep_sleep", event_label: "sleep", event_value: "5", event_time: "2026-09-18 05:58:26.573776", is_user_added: false },
        { event_name: "light_sleep", event_label: "sleep", event_value: "4", event_time: "2026-09-18 07:01:51.116649", is_user_added: false },
        { event_name: "quiet_awake", event_label: "stirring", event_value: "3", event_time: "2026-09-18 07:02:16.928391", is_user_added: false },
        { event_name: "deep_sleep", event_label: "sleep", event_value: "5", event_time: "2026-09-18 07:04:27.506550", is_user_added: false },
        { event_name: "active_awake", event_label: "awake", event_value: "2", event_time: "2026-09-18 11:20:46.944294", is_user_added: false },
        { event_name: "away", event_label: "away", event_value: "1", event_time: "2026-09-18 11:22:40.101291", is_user_added: false },
      ],
    };
    const parsed = parseHistory(live, "America/Los_Angeles");
    expect(parsed).toEqual({ sleeps: [{ start: "2026-09-18T05:58:26.573Z", end: "2026-09-18T11:20:46.944Z" }], unknownLabels: [], droppedIntervals: 0 });
    // 05:58:26Z is 10:58 pm Pacific on the 17th, which is what the Cradlewise app showed as bedtime.
    expect(new Date(parsed.sleeps[0].start).toLocaleTimeString("en-US", { timeZone: "America/Vancouver", hour: "numeric", minute: "2-digit" })).toBe("10:58 PM");
  });
  it("reads the NAPS banner as nap intervals and tolerates unknown banners", () => {
    const result = parseMetrics(
      {
        timezone: "America/Los_Angeles",
        metrics: [
          {
            date: "2026-09-17 08:00:00.000000",
            banners: [
              { type: "soothes", header: "SOOTHES", data: { value: 3, display_value: "3" } },
              { type: "naps", header: "NAPS", data: { value: 1, naps: [{ start_time: "2026-09-17 17:00:00.000000", end_time: "2026-09-17 17:45:00.000000", duration_in_mins: 45 }] } },
              { type: "info", header: "AWAKE IN BED", data: { value: 5476, display_value: "1h 31m" } },
              { type: "info", header: "SOMETHING NEW", data: { value: null } },
            ],
          },
        ],
      },
      "America/Los_Angeles",
    );
    expect(result).toMatchObject({ bed_min: null, rise_min: null, day_metrics: { awake_in_bed_s: 5476, naps: [{ start: t(0), end: t(45) }] } });
  });
  it("reconciles single provisional rows while preserving annotations and human ownership", () => {
    const history = [{ start: t(1), end: t(30) }];
    expect(reconcileSleep(history, [row()], ctx())[0]).toMatchObject({
      id: "s",
      version: t(0),
      started_at: t(1),
      ended_at: t(30),
      payload: { provisional: false },
    });
    expect(
      reconcileSleep(
        history,
        [row({ payload: { timing_locked: true } })],
        ctx(),
      ),
    ).toEqual([]);
    expect(reconcileSleep(history, [row({ deleted_at: t(4) })], ctx())).toEqual(
      [],
    );
  });
  it("does not destructively merge or split ambiguous history", () => {
    expect(
      reconcileSleep(
        [{ start: t(0), end: t(30) }],
        [row({ ended_at: t(10) }), row({ id: "s2", started_at: t(20) })],
        ctx(),
      ),
    ).toEqual([]);
    expect(
      reconcileSleep(
        [
          { start: t(0), end: t(10) },
          { start: t(20), end: t(30) },
        ],
        [row()],
        ctx(),
      ),
    ).toEqual([]);
  });
});
