import { createClient } from 'npm:@supabase/supabase-js@2.57.4';
import { deriveSleep } from './derive-sleep.ts';
import { parseStatus, parseMetrics, parseHistory, reconcileSleep } from './cradlewise.ts';
import { localParts, localInstant, shiftDate, validTimezone } from '../_shared/sleep-time.ts';

const API = 'https://integrations.cradlewise.com/api/v1';
const MAX_PAUSE_S = 3600; // never let one header pause polling for longer than an hour
// Settled (sleeping/away) states poll once a minute. The cron fires every 30 s, so the check runs
// at ~30 s and ~60 s after the last observation; a 60 s threshold lost the second tick to
// invocation jitter often enough to stretch the cadence to 90 s. 55 s lands on it reliably.
const CALM_POLL_MS = 55000;

const env = (key: string) => {
  const v = Deno.env.get(key);
  if (!v) throw new Error(`Missing ${key}`);
  return v;
};
const reply = (status: number, body: unknown) => new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json' } });

/** The documentation requires `YYYY-MM-DD HH:MM:SS` with the space encoded as %20; URLSearchParams would send `+`. */
const query = (params: Record<string, string>) =>
  Object.entries(params)
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
    .join('&');

async function sameSecret(a: string, b: string) {
  const enc = new TextEncoder();
  const [x, y] = await Promise.all([crypto.subtle.digest('SHA-256', enc.encode(a)), crypto.subtle.digest('SHA-256', enc.encode(b))]);
  const aa = new Uint8Array(x),
    bb = new Uint8Array(y);
  let diff = 0;
  for (let i = 0; i < aa.length; i++) diff |= aa[i] ^ bb[i];
  return diff === 0;
}

/** Seconds to wait from Retry-After (seconds or HTTP date) or X-RateLimit-Reset (Unix seconds), capped. */
function pauseSeconds(res: Response): number {
  const retry = res.headers.get('Retry-After'),
    reset = res.headers.get('X-RateLimit-Reset');
  let seconds = retry ? Number(retry) : 0;
  if (retry && !Number.isFinite(seconds)) seconds = Math.ceil((Date.parse(retry) - Date.now()) / 1000);
  if (!seconds && reset) seconds = Math.ceil(Number(reset) - Date.now() / 1000);
  return Number.isFinite(seconds) ? Math.min(MAX_PAUSE_S, Math.max(0, seconds)) : 0;
}

Deno.serve(async (req) => {
  if (req.method !== 'POST') return reply(405, { error: 'POST required' });
  if (!(await sameSecret(req.headers.get('x-scheduler-secret') ?? '', env('SCHEDULER_SECRET')))) return reply(401, { error: 'unauthorized' });
  const db = createClient(env('SUPABASE_URL'), env('SUPABASE_SERVICE_ROLE_KEY'), { auth: { persistSession: false } }),
    hh = env('HOUSEHOLD_ID'),
    cwToken = Deno.env.get('CW_TOKEN') ?? '';
  const rpc = async (name: string, args: Record<string, unknown>) => {
    const r = await db.rpc(name, args);
    if (r.error) throw r.error;
    return r.data;
  };
  try {
    async function all(build: () => any) {
      const rows: any[] = [];
      for (let offset = 0; ; offset += 500) {
        const r = await build().order('id').range(offset, offset + 499);
        if (r.error) throw r.error;
        rows.push(...r.data);
        if (r.data.length < 500) return rows;
      }
    }
    const state = await rpc('cw_lease', { hh });
    if (!state) return reply(200, { skipped: 'leased' });
    const token = state.lease_token,
      began = Date.now(),
      tz: string = validTimezone(state.timezone) ? state.timezone : 'UTC',
      ctx = { now: new Date().toISOString(), timezone: tz, bed_min: state.bed_min, rise_min: state.rise_min };
    const finish = (id: number | null, observation: unknown, patch: unknown) => rpc('cw_finish', { hh, token, request_id: id, observation, patch });
    if (!cwToken) {
      await finish(null, null, { error: 'token_missing', retry_seconds: 300 });
      return reply(200, { skipped: 'token_missing' });
    }
    /** Fetch one endpoint under admission. Household-wide errors (token, subscription, rate limit,
     *  transport) pause everything; a bad payload or a 4xx on a secondary endpoint is recorded on
     *  that endpoint only, so a metrics problem can never stop status polling. */
    async function fetchApi(endpoint_name: 'status' | 'metrics' | 'history', path: string, parse: (raw: unknown) => Record<string, unknown>) {
      if (Date.now() - began > 16000) return false;
      const id = await rpc('cw_admit', { hh, token, endpoint_name });
      if (!id) return false;
      const local = (reason: string) => (endpoint_name === 'status' ? { error: 'upstream' } : { [`${endpoint_name}_error`]: reason });
      try {
        const res = await fetch(API + path, { headers: { Authorization: `Bearer ${cwToken}` }, signal: AbortSignal.timeout(6000) });
        if (!res.ok) {
          const shared = res.status === 401 ? 'token_expired' : res.status === 403 ? 'forbidden' : res.status === 429 ? 'rate_limited' : res.status >= 500 ? 'upstream' : null;
          await finish(id, null, shared ? { error: shared, retry_seconds: pauseSeconds(res) } : local(`http_${res.status}`));
          return false;
        }
        let patch: Record<string, unknown>;
        try {
          patch = parse(await res.json());
        } catch {
          await finish(id, null, local('unsupported_payload'));
          return false;
        }
        if (res.headers.get('X-RateLimit-Remaining') === '0') {
          const pause = pauseSeconds(res);
          if (pause) patch.rate_pause_seconds = pause;
        }
        const observation = patch.observation ?? null;
        delete patch.observation;
        return await finish(id, observation, patch);
      } catch {
        // A timeout on the larger metrics or c-chart responses is that endpoint's problem; if the
        // network is really down, the next status poll fails too and pauses everything.
        await finish(id, null, endpoint_name === 'status' ? { error: 'transport' } : local('transport'));
        return false;
      }
    }
    const riseMinute = state.rise_min ?? 480;
    const riseClock = String(Math.floor(riseMinute / 60)).padStart(2, '0') + ':' + String(riseMinute % 60).padStart(2, '0') + ':00';
    const localDate = localParts(new Date(), tz).date;
    let lastRise = localInstant(localDate + ' ' + riseClock, tz);
    if (+lastRise > Date.now()) lastRise = localInstant(shiftDate(localDate, -1) + ' ' + riseClock, tz);
    // Cradlewise's day runs from its day-start setting (08:00 by default) and the metrics/c-chart
    // responses window themselves on whatever start_time we send, so ask from yesterday's 08:00 local
    // to now; that keeps rise and bedtime meaningful per app-day and covers 32-56 h of history.
    const nowLocal = localParts(new Date(), tz);
    const dayStart = `${shiftDate(nowLocal.date, nowLocal.minute < 480 ? -2 : -1)} 08:00:00`;
    const range = query({ start_time: dayStart, end_time: nowLocal.text });
    const calm = ['sleeping', 'away'].includes(state.status);
    if (!calm || !state.observed_at || Date.now() - Date.parse(state.observed_at) >= CALM_POLL_MS)
      await fetchApi('status', '/baby/status', (raw) => ({ observation: parseStatus(raw, new Date().toISOString()) }));
    // Always derive, even when admission/backoff blocked a fetch: stale open rows need closure.
    // Before derivation is switched on, fetch the sleep history once so the real response shape
    // can be inspected (sleep_status.history_raw) against the parser; nothing is written to entries.
    if (!state.derive_enabled && !state.history_at)
      await fetchApi('history', `/sleep/c-chart?${range}`, (body) => {
        const parsed = parseHistory(body, tz);
        const note = parsed.unknownLabels.length ? `unknown_labels:${parsed.unknownLabels.slice(0, 5).join(',')}` : parsed.droppedIntervals ? `dropped_intervals:${parsed.droppedIntervals}` : null;
        return { history_raw: body, history_ok: true, ...(note ? { history_error: note } : {}) };
      });
    if (state.derive_enabled) {
      const from = new Date(Date.now() - 36 * 3600000).toISOString();
      // Every row that overlaps the window (not only those starting in it), plus the open one.
      // Reconciliation acts only on history inside the same window (see reconcileSleep).
      const sleepRows = () =>
        all(() =>
          db
            .from('entries')
            .select('*')
            .eq('household_id', hh)
            .eq('type', 'sleep')
            .or(`started_at.gte.${from},ended_at.gte.${from},ended_at.is.null`)
        );
      const observations = await all(() => db.from('sleep_observations').select('id,status,since,observed_at').eq('household_id', hh).gte('observed_at', from));
      const actions = deriveSleep(observations, await sleepRows(), { ...ctx, now: new Date().toISOString() });
      if (actions.length) await rpc('cw_apply', { hh, token, actions });
      if (!state.history_at || Date.now() - Date.parse(state.history_at) >= 3600000 || Date.parse(state.history_at) < +lastRise) {
        let history: ReturnType<typeof parseHistory> | undefined;
        const ok = await fetchApi('history', `/sleep/c-chart?${range}`, (raw) => {
          history = parseHistory(raw, tz);
          return { history_raw: raw };
        });
        if (ok && history) {
          const applied = await rpc('cw_apply', { hh, token, actions: reconcileSleep(history.sleeps, await sleepRows(), ctx, from) });
          const note = history.unknownLabels.length ? `unknown_labels:${history.unknownLabels.slice(0, 5).join(',')}` : history.droppedIntervals ? `dropped_intervals:${history.droppedIntervals}` : null;
          if (applied) await finish(null, null, { history_ok: true, ...(note ? { history_error: note } : {}) });
        }
      }
    }
    if (!state.metrics_at || Date.now() - Date.parse(state.metrics_at) >= 1800000)
      await fetchApi('metrics', `/sleep/day-metrics?${range}`, (raw) => parseMetrics(raw, tz));
    return reply(200, { ok: true });
  } catch {
    return reply(500, { error: 'poll_failed' });
  } // Never include secrets/upstream bodies in logs or responses.
});
