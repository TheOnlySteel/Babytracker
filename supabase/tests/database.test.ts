import { PGlite } from '@electric-sql/pglite';
import { beforeAll, afterAll, describe, it, expect } from 'vitest';
import { readFileSync, readdirSync } from 'node:fs';
import postgres from 'postgres';
import { randomUUID } from 'node:crypto';
const nativeUrl = process.env.CRADLEWATCH_TEST_DATABASE_URL;
if (
  nativeUrl &&
  (new URL(nativeUrl).pathname !== '/cradlewatch_test' ||
    !['localhost', '127.0.0.1', 'postgres'].includes(
      new URL(nativeUrl).hostname,
    ))
)
  throw new Error(
    'Native test URL must target the disposable cradlewatch_test database',
  );
const nativeClient = (url: string) =>
  postgres(url, {
    max: 1,
    onnotice: () => {},
    // PGlite accepts raw JSON text. postgres.js normally stringifies that text a
    // second time once PostgreSQL infers jsonb; keep the two test transports equal.
    types: {
      json: {
        to: 3802,
        from: [114, 3802],
        serialize: (value: any) =>
          typeof value === 'string' ? value : JSON.stringify(value),
        parse: JSON.parse,
      },
    },
  });
const native = nativeUrl ? nativeClient(nativeUrl) : null;
const db = native
  ? {
      exec: (sql: string) => native.unsafe(sql),
      query: async (sql: string, params: any[] = []) => ({
        rows: await native.unsafe(sql, params),
      }),
      close: () => native.end(),
    }
  : new PGlite();
const hh = '10000000-0000-0000-0000-000000000001',
  other = '10000000-0000-0000-0000-000000000002';
const user = '20000000-0000-0000-0000-000000000001',
  foreignUser = '20000000-0000-0000-0000-000000000002';
let key: string, deviceId: string;
async function q(sql: string, params: any[] = []) {
  return (await db.query(sql, params)).rows as any[];
}
async function role(name: string, token?: string) {
  await db.exec('reset role');
  if (token !== undefined)
    await q("select set_config('request.headers',$1,false)", [
      JSON.stringify({ 'x-device-key': token }),
    ]);
  await db.exec(`set role ${name}`);
}
async function op(op: string, args = {}, extra = {}) {
  const envelope = {
    protocol: 1,
    client_op_id: randomUUID(),
    op,
    occurred_at: new Date().toISOString(),
    clock_ok: true,
    caregiver_id: user,
    args,
    ...extra,
  };
  return {
    envelope,
    result: (
      await q('select public.device_op($1::jsonb) result', [
        JSON.stringify(envelope),
      ])
    )[0].result,
  };
}
beforeAll(async () => {
  await db.exec(`create role anon; create role authenticated; create role service_role bypassrls;
    create schema auth; create table auth.users(id uuid primary key);
    create function auth.uid() returns uuid language sql stable as $$ select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid $$;
    grant usage on schema public,auth to anon,authenticated,service_role; grant execute on function auth.uid() to public;
    create publication supabase_realtime;`);
  for (const name of readdirSync('supabase/migrations').sort()) {
    // PGlite lacks pgcrypto; production's historical extension declaration is the only omission.
    // New migrations use built-in gen_random_uuid/sha256 and are executed unmodified.
    const sql = readFileSync(`supabase/migrations/${name}`, 'utf8').replace(
      'create extension if not exists pgcrypto;',
      native ? 'create extension if not exists pgcrypto;' : '',
    );
    try {
      await db.exec(sql);
    } catch (e) {
      const err = e as { position?: number; message: string };
      throw new Error(
        `${name}: ${err.message} at ${err.position}: ${sql.slice(Math.max(0, Number(err.position) - 150), Number(err.position) + 100)}`,
      );
    }
  }
  await q('insert into public.households(id) values($1),($2)', [hh, other]);
  await q('insert into auth.users values($1),($2)', [user, foreignUser]);
  await q(
    "insert into public.caregivers(user_id,household_id,display_name) values($1,$2,'A'),($3,$4,'B')",
    [user, hh, foreignUser, other],
  );
  await q(
    "insert into public.children(household_id,name,birth_date) values($1,'Synthetic','2026-01-01'),($2,'Other','2026-01-01')",
    [hh, other],
  );
  await q("select set_config('request.jwt.claim.sub',$1,false)", [user]);
  await role('authenticated');
  const paired = (await q("select public.pair_device('Test pad') d"))[0].d;
  key = paired.key;
  deviceId = paired.id;
}, 30000);
afterAll(() => db.close());
describe.sequential('database trust boundary and operation contract', () => {
  it('supports a fresh project without implicit API grants and preserves household isolation', async () => {
    await role('authenticated');
    expect(await q('select id from public.children')).toHaveLength(1);
    expect(await q('select user_id from public.caregivers')).toHaveLength(1);
    const row = (
      await q(
        "insert into public.entries(household_id,type,started_at,payload,created_by) values($1,'diaper',now(),'{\"wet\":true}',$2) returning id",
        [hh, user],
      )
    )[0];
    expect(
      await q(
        'update public.entries set note=$1,updated_by=$2 where id=$3 returning note',
        ['Phone edit', user, row.id],
      ),
    ).toEqual([{ note: 'Phone edit' }]);
    await q(
      "insert into public.household_prefs(household_id,prefs) values($1,'{}') on conflict(household_id) do update set prefs=excluded.prefs",
      [hh],
    );
    await expect(
      q(
        "insert into public.entries(household_id,type,started_at,created_by) values($1,'diaper',now(),$2)",
        [other, user],
      ),
    ).rejects.toThrow(/row-level security/);
    expect(
      await q('select id from public.households where id=$1', [other]),
    ).toHaveLength(0);
    await role('service_role');
    expect(await q('select id from public.children')).toHaveLength(2);
    expect(
      await q('select id from public.entries where id=$1', [row.id]),
    ).toHaveLength(1);
    await role('anon');
    await expect(q('select id from public.entries')).rejects.toThrow(
      /permission denied/,
    );
    await role('postgres');
    await q('delete from public.entries where id=$1', [row.id]);
    await role('authenticated');
  });
  it('keeps hashes and private helpers inaccessible', async () => {
    await role('authenticated');
    await expect(q('select key_hash from public.devices')).rejects.toThrow(
      /permission denied/,
    );
    await expect(q('select device.auth()')).rejects.toThrow(
      /permission denied/,
    );
    await expect(q('select public.cw_lease($1)', [hh])).rejects.toThrow(
      /permission denied/,
    );
    await expect(q("update public.devices set name='Changed'")).rejects.toThrow(
      /permission denied/,
    );
    const devices = await q('select id,name from public.devices');
    expect(devices).toHaveLength(1);
  });
  it('rejects missing authentication, even for snapshots', async () => {
    await role('anon', 'wrong');
    await expect(q('select public.device_snapshot(1)')).rejects.toThrow(
      /Invalid or revoked/,
    );
  });
  it('returns protocol, household caregivers, null timers and separate source age', async () => {
    await role('anon', key);
    const s = (await q('select public.device_snapshot(1) s'))[0].s;
    expect(s).toMatchObject({
      protocol: 1,
      bf: null,
      pump: null,
      sleep: null,
      source_observed_at: null,
      source_status: 'unknown',
    });
    expect(s.caregivers).toEqual([{ id: user, name: 'A' }]);
    expect(s.snapshot_at).toBeTruthy();
    expect((await q('select public.device_snapshot(2) s'))[0].s.error).toBe(
      'update_firmware',
    );
  });
  it('commits a one-shot once, deduplicates a lost response and rejects ID reuse', async () => {
    const { envelope, result } = await op('bottle', {
      ml: 80,
      kind: 'formula',
    });
    expect(result.outcome).toBe('applied');
    expect(result.row.via).toBe(`core2:${deviceId}`);
    const duplicate = (
      await q('select public.device_op($1) d', [JSON.stringify(envelope)])
    )[0].d;
    expect(duplicate.outcome).toBe('duplicate');
    expect(duplicate.row.id).toBe(result.row.id);
    const reused = (
      await q('select public.device_op($1) d', [
        JSON.stringify({ ...envelope, args: { ml: 90, kind: 'formula' } }),
      ])
    )[0].d;
    expect(reused.reason).toBe('op_id_reuse');
  });
  it('rejects foreign caregivers, unknown ops, unsynced clocks and stale timers', async () => {
    expect(
      (await op('diaper', { wet: true }, { caregiver_id: foreignUser })).result
        .outcome,
    ).toBe('invalid');
    // A one-shot log from an unsynced pad is kept at receipt time and flagged; a timer is not.
    const unsynced = (await op('diaper', { wet: true }, { clock_ok: false, occurred_at: '2001-01-01T00:00:00Z' })).result;
    expect(unsynced.outcome).toBe('applied');
    expect(unsynced.row.payload.time_uncertain).toBe(true);
    expect(Date.now() - Date.parse(unsynced.row.started_at)).toBeLessThan(60000);
    expect(
      (await op('bf_start', { side: 'left' }, { clock_ok: false })).result.reason,
    ).toBe('clock');
    // Malformed timestamps are an `invalid` outcome, never an HTTP 500.
    expect((await op('diaper', { wet: true }, { occurred_at: 'yesterday-ish' })).result.outcome).toBe('invalid');
    expect(
      (
        await op(
          'bf_start',
          { side: 'left' },
          { occurred_at: new Date(Date.now() - 60000).toISOString() },
        )
      ).result.reason,
    ).toBe('timer_requires_live_connection');
    expect((await op('do_anything')).result.reason).toBe('unknown_op');
  });
  it('does not permit targeting another household', async () => {
    await role('postgres');
    const foreign = (
      await q(
        "insert into public.entries(household_id,type,started_at,payload,via) values($1,'pump',now(),'{}',$2) returning id,updated_at",
        [other, `core2:${deviceId}`],
      )
    )[0];
    await role('anon', key);
    expect(
      (
        await op(
          'pump_stop',
          {},
          { target_id: foreign.id, expected_updated_at: foreign.updated_at },
        )
      ).result.reason,
    ).toBe('target');
  });
  it('serializes starts and requires exact versions for switches/stops', async () => {
    const first = (await op('bf_start', { side: 'left' })).result;
    expect(first.outcome).toBe('applied');
    expect((await op('bf_start', { side: 'right' })).result.outcome).toBe(
      'conflict',
    );
    const switched = (
      await op(
        'bf_switch',
        {},
        { target_id: first.row.id, expected_updated_at: first.row.updated_at },
      )
    ).result;
    expect(switched.outcome).toBe('applied');
    expect(switched.row.payload.segments).toHaveLength(2);
    expect(
      (
        await op(
          'bf_stop',
          {},
          {
            target_id: first.row.id,
            expected_updated_at: first.row.updated_at,
          },
        )
      ).result.outcome,
    ).toBe('conflict');
    const stopped = (
      await op(
        'bf_stop',
        {},
        {
          target_id: first.row.id,
          expected_updated_at: switched.row.updated_at,
        },
      )
    ).result;
    expect(stopped.outcome).toBe('applied');
    expect(stopped.row.ended_at).not.toBeNull();
    expect(
      (
        await op(
          'delete',
          {},
          {
            target_id: stopped.row.id,
            expected_updated_at: stopped.row.updated_at,
          },
        )
      ).result.outcome,
    ).toBe('applied');
  });
  it('stores unsplit pump totals and locks a human-ended sleep', async () => {
    const pump = (await op('pump_start')).result.row;
    const done = (
      await op(
        'pump_stop',
        { total_ml: 75 },
        { target_id: pump.id, expected_updated_at: pump.updated_at },
      )
    ).result;
    expect(done.row.payload.total_ml).toBe(75);
    const sleep = (await op('sleep_start', { place: 'crib' })).result.row;
    const end = (
      await op(
        'sleep_stop',
        {},
        { target_id: sleep.id, expected_updated_at: sleep.updated_at },
      )
    ).result;
    expect(end.row.payload.timing_locked).toBe(true);
    const stats = (await q('select public.device_sleep_today() s'))[0].s;
    expect(stats.naps).toBeGreaterThan(0);
  });
  it('revocation prevents cached replay', async () => {
    const { envelope } = await op('diaper', { wet: true });
    await role('authenticated');
    await q('select public.manage_device($1,null,true)', [deviceId]);
    await role('anon', key);
    await expect(
      q('select public.device_op($1)', [JSON.stringify(envelope)]),
    ).rejects.toThrow(/Invalid or revoked/);
  });
});
describe.sequential('poller admission, lease fencing and RLS', () => {
  it('takes one lease and reserves the rolling history allowance', async () => {
    await role('postgres');
    let s = (await q('select public.cw_lease($1) s', [hh]))[0].s;
    expect(s.lease_token).toBeTruthy();
    expect((await q('select public.cw_lease($1) s', [hh]))[0].s).toBeNull();
    for (let i = 0; i < 2; i++)
      expect(
        (
          await q("select public.cw_admit($1,$2,'status') id", [
            hh,
            s.lease_token,
          ])
        )[0].id,
      ).not.toBeNull();
    expect(
      (
        await q("select public.cw_admit($1,$2,'status') id", [
          hh,
          s.lease_token,
        ])
      )[0].id,
    ).toBeNull();
    await q(
      "insert into public.cw_requests(household_id,endpoint,at) select $1,'status',now()-interval '2 hours' from generate_series(1,2678)",
      [hh],
    );
    expect(
      (
        await q("select public.cw_admit($1,$2,'metrics') id", [
          hh,
          s.lease_token,
        ])
      )[0].id,
    ).toBeNull();
    expect(
      (
        await q("select public.cw_admit($1,$2,'history') id", [
          hh,
          s.lease_token,
        ])
      )[0].id,
    ).not.toBeNull();
    expect(
      (
        await q('select public.cw_finish($1,$2,null,null,$3) ok', [
          hh,
          randomUUID(),
          JSON.stringify({ error: 'upstream' }),
        ])
      )[0].ok,
    ).toBe(false);
  });
  it('respects derive_enabled and does not overwrite human versions', async () => {
    await role('postgres');
    await q(
      'update public.sleep_status set lease_until=null where household_id=$1',
      [hh],
    );
    const s = (await q('select public.cw_lease($1) s', [hh]))[0].s;
    const a = [
      {
        op: 'insert',
        source_key: 'cw:test',
        started_at: '2026-09-01T10:00:00Z',
        ended_at: '2026-09-01T11:00:00Z',
        payload: { source: 'cradlewise', kind: 'nap' },
      },
    ];
    expect(
      (
        await q('select public.cw_apply($1,$2,$3) ok', [
          hh,
          s.lease_token,
          JSON.stringify(a),
        ])
      )[0].ok,
    ).toBe(false);
    await q(
      'update public.sleep_status set derive_enabled=true where household_id=$1',
      [hh],
    );
    expect(
      (
        await q('select public.cw_apply($1,$2,$3) ok', [
          hh,
          s.lease_token,
          JSON.stringify(a),
        ])
      )[0].ok,
    ).toBe(true);
    const e = (
      await q("select * from public.entries where source_key='cw:test'")
    )[0];
    const update = [
      {
        op: 'update',
        id: e.id,
        version: '2000-01-01T00:00:00Z',
        ended_at: '2026-09-01T12:00:00Z',
      },
    ];
    expect(
      (
        await q('select public.cw_apply($1,$2,$3) ok', [
          hh,
          s.lease_token,
          JSON.stringify(update),
        ])
      )[0].ok,
    ).toBe(false);
  });
  it('cannot read another household or mutate status through table grants', async () => {
    await role('authenticated');
    expect(await q('select household_id from public.sleep_status')).toEqual([
      { household_id: hh },
    ]);
    // The poll lease token is not a caregiver's business: the column grant hides it.
    await expect(q('select lease_token from public.sleep_status')).rejects.toThrow(/permission denied/);
    await expect(q('select * from public.cw_requests')).rejects.toThrow(
      /permission denied/,
    );
    await expect(
      q('update public.sleep_status set derive_enabled=true'),
    ).rejects.toThrow(/permission denied/);
    await q("select set_config('request.jwt.claim.sub',$1,false)", [
      foreignUser,
    ]);
    expect(await q('select household_id from public.sleep_status')).toEqual([]);
  });
});

describe.sequential('0010: policy initplans, anon grants and lean poll payloads', () => {
  it('pins the RLS helper to an empty search_path', async () => {
    await role('postgres');
    const [f] = await q(
      "select p.proconfig, p.prosecdef from pg_proc p where p.oid = 'public.my_household_id()'::regprocedure",
    );
    expect(f.prosecdef).toBe(true);
    expect(f.proconfig).toEqual(['search_path=""']);
  });
  it('evaluates identity once per statement in every policy', async () => {
    await role('postgres');
    const policies = await q(
      "select tablename, policyname, coalesce(qual,'') || ' ' || coalesce(with_check,'') expr from pg_policies where schemaname='public'",
    );
    expect(policies.length).toBeGreaterThanOrEqual(13);
    for (const p of policies) {
      // Every call must sit inside a scalar sub-select, which the planner runs once as an initPlan.
      const calls = p.expr.match(/(my_household_id|auth\.uid)\(\)/g) ?? [];
      const wrapped = p.expr.match(/SELECT (public\.)?(my_household_id|auth\.uid)\(\)/g) ?? [];
      expect({ policy: p.policyname, calls: calls.length }).toEqual({ policy: p.policyname, calls: wrapped.length });
      expect(calls.length).toBeGreaterThan(0);
    }
    // Behaviour is unchanged: the caller still sees and writes only their own household.
    await q("select set_config('request.jwt.claim.sub',$1,false)", [user]);
    await role('authenticated');
    expect(await q('select id from public.households')).toEqual([{ id: hh }]);
    await expect(
      q("insert into public.entries(household_id,type,started_at,created_by) values($1,'diaper',now(),$2)", [hh, foreignUser]),
    ).rejects.toThrow(/row-level security/);
  });
  it('gives anon nothing on the base tables and no API role TRUNCATE', async () => {
    await role('postgres');
    const rows = await q(
      `select c.relname, r.rolname, p.priv from pg_class c join pg_namespace n on n.oid=c.relnamespace
         cross join (values ('anon'),('authenticated')) r(rolname)
         cross join (values ('select'),('insert'),('update'),('delete'),('truncate'),('references'),('trigger')) p(priv)
       where n.nspname='public' and c.relkind='r' and has_table_privilege(r.rolname, c.oid, p.priv)
         and (r.rolname='anon' or p.priv in ('truncate','references','trigger'))`,
    );
    expect(rows).toEqual([]);
  });
  it('keeps the raw c-chart and day metrics out of the lease and the settings RPC', async () => {
    await role('postgres');
    await q(
      `update public.sleep_status set lease_until=null, history_raw='{"events":[]}', day_metrics='{"awake_in_bed_s":1}' where household_id=$1`,
      [hh],
    );
    const s = (await q('select public.cw_lease($1) s', [hh]))[0].s;
    expect(s.lease_token).toBeTruthy();
    expect(s.timezone).toBe('America/Los_Angeles');
    expect(s).toHaveProperty('derive_enabled');
    expect(s).toHaveProperty('history_at');
    expect(s).not.toHaveProperty('history_raw');
    expect(s).not.toHaveProperty('day_metrics');
    await q("select set_config('request.jwt.claim.sub',$1,false)", [user]);
    await role('authenticated');
    const m = (await q('select public.monitor_settings() m'))[0].m;
    expect(m).not.toHaveProperty('history_raw');
    expect(m).not.toHaveProperty('lease_token');
    expect(m.day_metrics).toEqual({ awake_in_bed_s: 1 });
    expect(m.requests_24h).toBeGreaterThan(0);
  });
});

describe.skipIf(!nativeUrl)(
  'native PostgreSQL concurrency (separate connections)',
  () => {
    it('admits exactly two status requests under concurrent callers', async () => {
      await role('postgres');
      await q('delete from public.cw_requests where household_id=$1', [hh]);
      await q(
        'update public.sleep_status set lease_until=null,retry_at=null where household_id=$1',
        [hh],
      );
      const lease = (await q('select public.cw_lease($1) s', [hh]))[0].s;
      const clients = Array.from({ length: 8 }, () => nativeClient(nativeUrl!));
      try {
        const results = await Promise.all(
          clients.map(
            (c) =>
              c`select public.cw_admit(${hh}::uuid,${lease.lease_token}::uuid,'status') id`,
          ),
        );
        expect(results.filter((r) => r[0].id !== null)).toHaveLength(2);
      } finally {
        await Promise.all(clients.map((c) => c.end()));
      }
    });
    it('serializes same-ID replays on one device and competing timer starts across two devices', async () => {
      await role('postgres');
      await q("select set_config('request.jwt.claim.sub',$1,false)", [user]);
      await role('authenticated');
      const devices = await Promise.all([
        q("select public.pair_device('Race A') d"),
        q("select public.pair_device('Race B') d"),
      ]);
      const keys = devices.map((r) => r[0].d.key);
      const clients = keys.map(() => nativeClient(nativeUrl!));
      try {
        await Promise.all(
          clients.map(
            (c, i) =>
              c`select set_config('request.headers',${JSON.stringify({ 'x-device-key': keys[i] })},false)`,
          ),
        );
        await Promise.all(clients.map((c) => c.unsafe('set role anon')));
        const envelope = {
          protocol: 1,
          client_op_id: randomUUID(),
          op: 'pump_start',
          occurred_at: new Date().toISOString(),
          clock_ok: true,
          caregiver_id: user,
          args: {},
        };
        const results = await Promise.all(
          clients.map(
            (c) =>
              c`select public.device_op(${JSON.stringify(envelope)}::jsonb) result`,
          ),
        );
        expect(results.map((r) => r[0].result.outcome).sort()).toEqual([
          'applied',
          'conflict',
        ]);
        const winner = results.findIndex(
          (r) => r[0].result.outcome === 'applied',
        );
        const replay = await clients[
          winner
        ]`select public.device_op(${JSON.stringify(envelope)}::jsonb) result`;
        expect(replay[0].result.outcome).toBe('duplicate');
      } finally {
        await Promise.all(clients.map((c) => c.end()));
      }
    });
  },
);
