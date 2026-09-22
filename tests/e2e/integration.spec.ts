import { test, expect } from "@playwright/test";
import { fixture, entry } from "./fixture";
test("sleep freshness and explicit auto End on the home card", async ({
  page,
}) => {
  const state = await fixture(page, [
    entry("sleep", {
      ended_at: null,
      payload: {
        source: "cradlewise",
        kind: "nap",
        place: "crib",
        provisional: true,
      },
      source_key: "cw:test",
    }),
  ]);
  await expect(
    page.getByRole("heading", { name: "Sleep", exact: true }),
  ).toBeVisible();
  await expect(page.getByText(/crib update 12 min old/)).toBeVisible();
  await page.screenshot({
    path: "test-results/sleep-home.png",
    fullPage: true,
  });
  await page.getByRole("button", { name: "End", exact: true }).click();
  await expect.poll(() => state.writes.length).toBe(1);
  expect(state.writes[0].payload.timing_locked).toBe(true);
  expect(state.writes[0].ended_at).toBeTruthy();
});
test("note-only sleep edits preserve timing ownership and unknown provenance", async ({
  page,
}) => {
  const state = await fixture(page, [
    entry("sleep", {
      payload: {
        source: "cradlewise",
        kind: "nap",
        place: "crib",
        provisional: true,
        future_field: "preserve",
      },
    }),
  ]);
  await page.getByRole("button", { name: /Last sleep/ }).click();
  await page.getByRole("textbox", { name: "Notes" }).fill("Quiet room");
  await page.getByRole("button", { name: "Save", exact: true }).last().click();
  await expect.poll(() => state.writes.length).toBe(1);
  expect(state.writes[0].payload.future_field).toBe("preserve");
  expect(state.writes[0].payload.timing_locked).not.toBe(true);
  expect(state.writes[0].ended_at).toBeUndefined();
});
test("earlier sleep form toggles end time and creates a completed entry", async ({
  page,
}) => {
  const state = await fixture(page);
  await page.getByRole("button", { name: "Add Sleep", exact: true }).click();
  await page.getByRole("button", { name: "Log an earlier nap" }).click();
  await expect(page.getByLabel("End Time")).toBeAttached();
  await page.getByRole("button", { name: "Save", exact: true }).last().click();
  await expect.poll(() => state.writes.length).toBe(1);
  expect(state.writes[0].ended_at).not.toBeNull();
});
test("pump stop commits elapsed time and supports an unsplit total", async ({
  page,
}) => {
  const start = new Date(Date.now() - 7 * 60000 - 14000).toISOString();
  const state = await fixture(page, [
    entry("pump", {
      started_at: start,
      ended_at: null,
      payload: { future_field: 17 },
    }),
  ]);
  await page.getByRole("button", { name: /Pumping/ }).click();
  await page.getByLabel("Total (mL)").fill("75");
  await page.getByRole("button", { name: "Stop pump", exact: true }).click();
  await expect.poll(() => state.writes.length).toBe(1);
  expect(
    Math.round(
      (Date.parse(state.writes[0].ended_at) - Date.parse(start)) / 1000,
    ),
  ).toBeGreaterThanOrEqual(434);
  expect(Date.parse(state.writes[0].ended_at) - Date.parse(start)).toBeLessThan(
    8 * 60000,
  );
  expect(state.writes[0].payload).toMatchObject({
    total_ml: 75,
    future_field: 17,
  });
});
test("unknown future entries cannot open the pump editor", async ({ page }) => {
  await fixture(page, [entry("future_type")]);
  await page.getByRole("link", { name: "History" }).click();
  await page.getByRole("button", { name: /Unknown entry/ }).click();
  await expect(
    page.getByText("Update the app to edit this entry"),
  ).toBeVisible();
  await expect(page.getByRole("dialog")).toHaveCount(0);
});
test("failed writes preserve the sleep draft", async ({ page }) => {
  const state = await fixture(page);
  await page.getByRole("button", { name: "Add Sleep", exact: true }).click();
  state.failWrites = true;
  await page.getByRole("button", { name: "Start sleep", exact: true }).click();
  await expect(
    page.getByText("Could not save. Your changes are still here."),
  ).toBeVisible();
  await expect(page.getByRole("dialog")).toBeVisible();
});
test("keeping mine after the other phone stopped the pump keeps their stop time", async ({
  page,
}) => {
  const start = new Date(Date.now() - 30 * 60000).toISOString();
  const state = await fixture(page, [
    entry("pump", { started_at: start, ended_at: null, payload: {} }),
  ]);
  await page.getByRole("button", { name: /Pumping/ }).click();
  await page.getByLabel("Total (mL)").fill("80");
  // The other phone stops it at 10 minutes while this sheet is open.
  const stoppedAt = new Date(Date.parse(start) + 10 * 60000).toISOString();
  Object.assign(state.rows[0], {
    ended_at: stoppedAt,
    updated_at: new Date().toISOString(),
  });
  await page.getByRole("button", { name: "Stop pump", exact: true }).click();
  await page.getByRole("button", { name: "Keep mine" }).click();
  await expect.poll(() => state.writes.length).toBe(2);
  expect(state.writes[1].ended_at).toBeUndefined();
  expect(state.writes[1].payload).toMatchObject({ total_ml: 80 });
  expect(state.rows[0].ended_at).toBe(stoppedAt);
});
