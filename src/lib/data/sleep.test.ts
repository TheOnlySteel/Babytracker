import { describe, it, expect } from "vitest";
import { summarize, nightSummary, windowFor, groupByDay, latestNightKey } from "./summary";
import { sleepKind, localInstant, sourceKey } from "./sleep-time";
import { entryLabel, entryMagnitude, editorFor, iconFor } from "./derive";
import { stopPatch } from "./timers";
import type { Entry } from "./types";
const row = (
  start: string,
  end: string | null,
  payload: object = { kind: "nap", source: "manual" },
  type: Entry["type"] = "sleep",
): Entry => ({
  id: start,
  household_id: "h",
  child_id: "c",
  type,
  started_at: start,
  ended_at: end,
  payload: payload as Entry["payload"],
  note: null,
  created_by: null,
  updated_by: null,
  created_at: start,
  updated_at: start,
  deleted_at: null,
  nara_activity_key: null,
});
describe("sleep summaries and dispatchers", () => {
  it("clips a cross-midnight night and counts naps by their start", () => {
    const e = row("2026-09-16T20:30:00Z", "2026-09-17T07:00:00Z", {
      kind: "night",
    });
    const s = summarize(
      [e],
      new Date("2026-09-17T00:00Z"),
      new Date("2026-09-18T00:00Z"),
    );
    expect(s.sleep.sleep_s).toBe(7 * 3600);
    expect(s.sleep.naps).toBe(0);
  });
  it("clips a rolling boundary and an unfinished nap to the same clock", () => {
    const e = row("2026-09-16T11:00Z", null);
    const s = summarize(
      [e],
      new Date("2026-09-16T12:00Z"),
      new Date("2026-09-18T00:00Z"),
      undefined,
      new Date("2026-09-17T12:00Z"),
    );
    expect(s.sleep.sleep_s).toBe(86400);
    expect(s.sleep.naps).toBe(0);
  });
  it("uses real elapsed time across a DST night and rejects ambiguous vendor wall time", () => {
    const start = localInstant("2026-11-01 00:30:00", "America/Los_Angeles"),
      end = localInstant("2026-11-01 07:00:00", "America/Los_Angeles");
    expect((+end - +start) / 3600000).toBe(7.5);
    // Vendor timestamps are parsed strictly: a repeated hour is never guessed.
    expect(() =>
      localInstant("2026-11-01 01:30:00", "America/Los_Angeles", "strict"),
    ).toThrow(/Ambiguous/);
  });
  it("groups one night across midnight without phantom wakings on a second night", () => {
    const es = [
      row("2026-09-16T20:00Z", "2026-09-16T23:00Z", {
        kind: "night",
        night_key: "2026-09-16",
      }),
      row("2026-09-17T00:00Z", "2026-09-17T02:00Z", {
        kind: "night",
        night_key: "2026-09-16",
      }),
      row("2026-09-17T03:00Z", "2026-09-17T07:00Z", {
        kind: "night",
        night_key: "2026-09-16",
      }),
      row("2026-09-17T20:00Z", "2026-09-18T07:00Z", {
        kind: "night",
        night_key: "2026-09-17",
      }),
    ];
    expect(nightSummary(es, "2026-09-16", new Date("2026-09-19")).wakings).toBe(
      2,
    );
  });
  it("gets household midnight, classification and canonical second keys", () => {
    expect(
      windowFor(
        "today",
        new Date("2026-09-17T12:00Z"),
        "America/Vancouver",
      ).from.toISOString(),
    ).toBe("2026-09-17T07:00:00.000Z");
    expect(sleepKind("2026-09-17T09:00Z", "America/Vancouver")).toEqual({
      kind: "night",
      night_key: "2026-09-16",
    });
    expect(sourceKey("2026-09-17T01:00:00.123Z")).toBe(
      "cw:2026-09-17T01:00:00Z",
    );
  });
  it("has explicit safe editors/icons for every known type and a future type", () => {
    for (const type of [
      "bottle",
      "breastfeed",
      "combo",
      "diaper",
      "pump",
      "sleep",
      "growth",
      "future",
    ]) {
      const e = row(
        "2026-09-17T10:00Z",
        "2026-09-17T11:00Z",
        {},
        type as Entry["type"],
      );
      expect(entryLabel(e)).toBeTypeOf("string");
      expect(iconFor(e)).toBeTruthy();
      expect(editorFor(type)).toBe(
        type === "growth" || type === "future"
          ? null
          : type === "combo"
            ? "breastfeed"
            : type,
      );
    }
  });
  it("prefers unsplit totals and stops pumps at real elapsed time", () => {
    const e = row(
      "2026-09-17T10:00Z",
      null,
      { total_ml: 80, left_ml: 20, right_ml: 20 },
      "pump",
    );
    expect(entryMagnitude(e)?.value).toBe(80);
    const now = new Date("2026-09-17T10:07:14Z");
    expect(stopPatch(e, now).ended_at).toBe(now.toISOString());
    expect(summarize([e], new Date("2026-09-17"), now).pump.total_ml).toBe(80);
  });
  it("reports the longest nap by its own length even when it straddles the window", () => {
    const from = new Date("2026-09-17T07:00:00Z"), to = new Date("2026-09-17T19:00:00Z");
    const s = summarize(
      [row("2026-09-17T06:30:00Z", "2026-09-17T08:00:00Z"), row("2026-09-17T12:00:00Z", "2026-09-17T12:40:00Z")],
      from, to, undefined, to,
    );
    expect(s.sleep.longest_nap_s).toBe(5400); // 90 min, not the 60 min inside the window
    expect(s.sleep.nap_s).toBe(3600 + 2400);
    expect(s.sleep.naps).toBe(1); // counted by start
  });
  it("lists a cross-midnight night under the day it began, once", () => {
    const night = row("2026-09-16T20:30:00-07:00", "2026-09-17T07:00:00-07:00", { kind: "night", night_key: "2026-09-16" });
    const days = groupByDay([night, row("2026-09-17T13:00:00-07:00", "2026-09-17T14:00:00-07:00")], "America/Los_Angeles");
    expect(days.map((d) => [d.day, d.entries.length])).toEqual([["2026-09-17", 1], ["2026-09-16", 1]]);
  });
  it("opens the last-night window on the most recent completed night", () => {
    const rows = [
      row("2026-09-16T20:30:00-07:00", "2026-09-17T01:00:00-07:00", { kind: "night", night_key: "2026-09-16" }),
      row("2026-09-17T01:20:00-07:00", "2026-09-17T07:00:00-07:00", { kind: "night", night_key: "2026-09-16" }),
    ];
    const now = new Date("2026-09-17T15:00:00-07:00");
    expect(latestNightKey(rows, now, "America/Los_Angeles")).toBe("2026-09-16");
    const w = windowFor("last-night", now, "America/Los_Angeles", rows);
    expect(w.from.toISOString()).toBe(new Date("2026-09-16T20:30:00-07:00").toISOString());
    expect(w.to.toISOString()).toBe(new Date("2026-09-17T07:00:00-07:00").toISOString());
  });
  it("resolves a skipped or repeated local hour leniently for our own day boundaries", () => {
    // America/Santiago springs forward at midnight: 2026-09-06 00:00 local does not exist.
    const gap = localInstant("2026-09-06 00:00:00", "America/Santiago", "lenient");
    expect(Number.isFinite(+gap)).toBe(true);
    expect(() => localInstant("2026-09-06 00:00:00", "America/Santiago", "strict")).toThrow();
    // Fall back in Los Angeles: 01:30 happens twice; lenient takes the first.
    const first = localInstant("2026-11-01 01:30:00", "America/Los_Angeles", "lenient");
    expect(first.toISOString()).toBe("2026-11-01T08:30:00.000Z");
    expect(() => localInstant("2026-11-01 01:30:00", "America/Los_Angeles", "strict")).toThrow();
  });
});
