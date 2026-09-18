import type { Page } from "@playwright/test";
const user = "20000000-0000-0000-0000-000000000001",
  hh = "10000000-0000-0000-0000-000000000001";
export function entry(
  type: string,
  patch: Record<string, any> = {},
): Record<string, any> {
  const start = new Date(Date.now() - 10 * 60000).toISOString();
  return {
    id: crypto.randomUUID(),
    household_id: hh,
    child_id: "child",
    type,
    started_at: start,
    ended_at: start,
    payload: {},
    note: null,
    created_by: user,
    updated_by: user,
    updated_at: start,
    created_at: start,
    deleted_at: null,
    source_key: null,
    via: "web",
    ...patch,
  };
}
export async function fixture(
  page: Page,
  rows: ReturnType<typeof entry>[] = [],
) {
  const writes: Array<Record<string, any>> = [];
  const state = {
    rows,
    writes,
    status: {
      household_id: hh,
      status: "sleeping",
      since: new Date(Date.now() - 600000).toISOString(),
      observed_at: new Date(Date.now() - 12 * 60000).toISOString(),
      source_error: null,
      derive_enabled: false,
      timezone: "America/Los_Angeles",
      requests_24h: 500,
    },
    failWrites: false,
  };
  await page.addInitScript(
    ({ user }) => {
      const exp = Math.floor(Date.now() / 1000) + 3600;
      const jwt =
        btoa(JSON.stringify({ alg: "HS256", typ: "JWT" })) +
        "." +
        btoa(JSON.stringify({ sub: user, role: "authenticated", exp })) +
        ".test";
      localStorage.setItem(
        "sb-fixture-auth-token",
        JSON.stringify({
          access_token: jwt,
          refresh_token: "fixture-refresh",
          expires_in: 3600,
          expires_at: exp,
          token_type: "bearer",
          user: {
            id: user,
            email: "fixture@example.invalid",
            aud: "authenticated",
            role: "authenticated",
          },
        }),
      );
    },
    { user },
  );
  await page.routeWebSocket("**/realtime/v1/**", (ws) => {
    ws.onMessage((raw) => {
      const m = JSON.parse(String(raw));
      if (m.event === "phx_join" || m.event === "heartbeat")
        ws.send(
          JSON.stringify({
            ...m,
            event: "phx_reply",
            payload: { status: "ok", response: { postgres_changes: [] } },
          }),
        );
    });
  });
  await page.route("https://fixture.supabase.co/**", async (route) => {
    const req = route.request(),
      url = new URL(req.url()),
      table = url.pathname.split("/").at(-1),
      method = req.method();
    if (method === "OPTIONS")
      return route.fulfill({
        status: 204,
        headers: {
          "access-control-allow-origin": "*",
          "access-control-allow-headers": "*",
        },
      });
    let data: any = [];
    const body = req.postDataJSON();
    if (url.pathname.includes("/auth/"))
      data = { id: user, email: "fixture@example.invalid" };
    else if (table === "caregivers")
      data = [{ user_id: user, household_id: hh, display_name: "Alex" }];
    else if (table === "children")
      data = [
        {
          id: "child",
          household_id: hh,
          name: "Baby",
          birth_date: "2026-01-01",
        },
      ];
    else if (table === "household_prefs") data = { prefs: {} };
    else if (table === "monitor_settings") {
      if (body?.enabled !== undefined)
        state.status.derive_enabled = body.enabled;
      data = state.status;
    } else if (table === "devices") data = [];
    else if (table === "entries") {
      let selected = state.rows.filter((e) => {
        for (const [k, v] of url.searchParams) {
          if (v === "is.null" && e[k] !== null) return false;
          if (v.startsWith("eq.") && String(e[k]) !== v.slice(3)) return false;
          if (v.startsWith("gte.") && String(e[k]) < v.slice(4)) return false;
          if (v.startsWith("lt.") && String(e[k]) >= v.slice(3)) return false;
          if (v.startsWith("in.") && !v.slice(4, -1).split(",").includes(e[k]))
            return false;
        }
        return true;
      });
      if (method === "PATCH" || method === "POST") {
        if (state.failWrites)
          return route.fulfill({
            status: 503,
            json: { message: "Synthetic failure" },
          });
        state.writes.push(body);
        if (method === "POST") {
          const saved = entry(body.type, {
            ...body,
            updated_at: new Date().toISOString(),
          });
          state.rows.push(saved);
          data = saved;
        } else {
          for (const e of selected)
            Object.assign(e, body, { updated_at: new Date().toISOString() });
          data = selected;
        }
      } else
        data = req.headers()["accept"]?.includes("vnd.pgrst.object")
          ? (selected[0] ?? null)
          : selected;
    }
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify(data),
      headers: { "access-control-allow-origin": "*" },
    });
  });
  await page.goto("/");
  return state;
}
