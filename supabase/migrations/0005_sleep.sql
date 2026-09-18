-- Explicit Data API grants. On a project created through the dashboard these already exist
-- (default privileges) and the statements are no-ops; the disposable-database tests start from
-- nothing, so they are stated. RLS still scopes caregiver reads/writes to their household.
grant usage on schema public to authenticated, service_role;
grant select on public.households, public.children, public.caregivers to authenticated, service_role;
grant select, insert, update on public.entries, public.household_prefs to authenticated;
grant select, insert, update on public.entries to service_role;
grant select on public.household_prefs to service_role;

alter table public.households add column timezone text not null default 'America/Los_Angeles';
alter table public.entries
  add column source_key text,
  add column via text not null default 'web',
  add constraint entries_household_source_key_key unique (household_id, source_key),
  add constraint entries_pump_total_check check (coalesce((payload->>'total_ml')::numeric, 0) >= 0);
-- Backfill provenance for the imported history without touching updated_at: a bumped version
-- would break every phone's conditional update mid-edit and broadcast 800 rows over Realtime.
alter table public.entries disable trigger entries_set_updated_at;
update public.entries set via = 'nara' where nara_activity_key is not null;
alter table public.entries enable trigger entries_set_updated_at;
create unique index entries_one_running_sleep on public.entries(household_id)
  where type = 'sleep' and ended_at is null and deleted_at is null;

create table public.sleep_observations (
  id bigint generated always as identity primary key,
  household_id uuid not null references public.households on delete cascade,
  status text not null check (status in ('sleeping','awake','stirring','crying','away','unknown')),
  since timestamptz, bounce text, music text, upstream_at timestamptz,
  observed_at timestamptz not null, raw jsonb
);
create index sleep_observations_hh_at on public.sleep_observations(household_id, observed_at desc);
create table public.sleep_status (
  household_id uuid primary key references public.households on delete cascade,
  status text not null default 'unknown', since timestamptz, bounce text, music text,
  observed_at timestamptz, source_error text, history_error text, metrics_error text,
  bed_min int check (bed_min between 0 and 1439), rise_min int check (rise_min between 0 and 1439),
  day_metrics jsonb, metrics_at timestamptz, history_at timestamptz,
  derive_enabled boolean not null default false,
  lease_until timestamptz, lease_token uuid, retry_at timestamptz, failures int not null default 0,
  updated_at timestamptz not null default now()
);
create table public.cw_requests (
  id bigint generated always as identity primary key,
  household_id uuid not null references public.households on delete cascade,
  endpoint text not null check(endpoint in ('status','metrics','history')),
  at timestamptz not null default now(), ok boolean
);
create index cw_requests_hh_at on public.cw_requests(household_id, at desc);
alter table public.sleep_observations enable row level security;
alter table public.sleep_status enable row level security;
alter table public.cw_requests enable row level security;
revoke all on public.sleep_observations, public.sleep_status, public.cw_requests from anon, authenticated;
grant select on public.sleep_observations to authenticated;
-- Caregivers may read the monitor's state but never the poll lease token.
grant select (household_id, status, since, bounce, music, observed_at, source_error, history_error, metrics_error,
  bed_min, rise_min, day_metrics, metrics_at, history_at, derive_enabled, retry_at, failures, updated_at)
  on public.sleep_status to authenticated;
grant all on public.sleep_observations, public.sleep_status, public.cw_requests to service_role;
grant usage, select on sequence public.sleep_observations_id_seq, public.cw_requests_id_seq to service_role;
create policy sleep_observations_read on public.sleep_observations for select to authenticated using(household_id = public.my_household_id());
create policy sleep_status_read on public.sleep_status for select to authenticated using(household_id = public.my_household_id());
alter publication supabase_realtime add table public.sleep_status;
create trigger sleep_status_updated before update on public.sleep_status for each row execute function public.set_updated_at();

-- A service lease is fenced by a random token. A late invocation cannot commit over a successor.
create function public.cw_lease(hh uuid) returns jsonb language plpgsql security definer set search_path = '' as $$
declare s public.sleep_status;
begin
  insert into public.sleep_status(household_id) values(hh) on conflict do nothing;
  update public.sleep_status set lease_token = gen_random_uuid(), lease_until = clock_timestamp() + interval '25 seconds'
    where household_id = hh and (lease_until is null or lease_until < clock_timestamp())
    returning * into s;
  if not found then return null; end if;
  return to_jsonb(s) || jsonb_build_object('timezone', (select timezone from public.households where id=hh));
end $$;

-- Serialization is necessary: count+insert alone is NOT atomic admission under MVCC.
create function public.cw_admit(hh uuid, token uuid, endpoint_name text) returns bigint language plpgsql security definer set search_path = '' as $$
declare s public.sleep_status; n bigint; total_n int; minute_n int; hour_n int; zone text; last_rise timestamptz;
begin
  select * into s from public.sleep_status where household_id=hh for update;
  if s.lease_token is distinct from token or s.lease_until < clock_timestamp() or s.retry_at > now() then return null; end if;
  if endpoint_name not in ('status','metrics','history') then return null; end if;
  select timezone into zone from public.households where id=hh;
  last_rise:=(date_trunc('day',now() at time zone zone)+make_interval(mins=>coalesce(s.rise_min,480))) at time zone zone;
  if last_rise>now() then last_rise:=((last_rise at time zone zone)-interval '1 day') at time zone zone; end if;
  if exists(select 1 from public.cw_requests where household_id=hh and endpoint=endpoint_name and (endpoint_name<>'history' or at>=last_rise) and at>now()-(case when endpoint_name='history' then interval '1 hour' when endpoint_name='metrics' then interval '30 minutes' else interval '0 seconds' end)) then return null; end if;
  select count(*), count(*) filter(where endpoint='status' and at > now()-interval '60 seconds'),
    count(*) filter(where endpoint <> 'status' and at > now()-interval '1 hour')
    into total_n,minute_n,hour_n from public.cw_requests where household_id=hh and at > now()-interval '24 hours';
  if total_n >= (case when endpoint_name='history' then 2880 else 2680 end)
    or (endpoint_name='status' and minute_n >= 2) or (endpoint_name<>'status' and hour_n >= 60) then return null; end if;
  insert into public.cw_requests(household_id,endpoint) values(hh,endpoint_name) returning id into n;
  return n;
end $$;

create function public.cw_finish(hh uuid, token uuid, request_id bigint, observation jsonb default null, patch jsonb default '{}')
returns boolean language plpgsql security definer set search_path = '' as $$
declare s public.sleep_status;
begin
  select * into s from public.sleep_status where household_id=hh for update;
  if s.lease_token is distinct from token or s.lease_until < clock_timestamp() then return false; end if;
  update public.cw_requests set ok=not(patch ? 'error' or patch ? 'history_error' or patch ? 'metrics_error') where id=request_id and household_id=hh;
  if observation is not null and (s.observed_at is null or (observation->>'observed_at')::timestamptz>s.observed_at) then
    insert into public.sleep_observations(household_id,status,since,bounce,music,upstream_at,observed_at,raw)
      values(hh,observation->>'status',(observation->>'since')::timestamptz,observation->>'bounce',observation->>'music',
        (observation->>'upstream_at')::timestamptz,(observation->>'observed_at')::timestamptz,observation->'raw');
    update public.sleep_status set status=observation->>'status',since=(observation->>'since')::timestamptz,
      bounce=observation->>'bounce',music=observation->>'music',observed_at=(observation->>'observed_at')::timestamptz,
      source_error=null, failures=0, retry_at=null where household_id=hh;
  end if;
  if patch ? 'error' then
    update public.sleep_status set source_error=patch->>'error',failures=failures+1,
      retry_at=now()+ make_interval(secs=>least(3600,greatest(coalesce((patch->>'retry_seconds')::int,0),least(300,30*power(2,least(failures,4)))::int))) where household_id=hh;
  end if;
  if patch ? 'rate_pause_seconds' then update public.sleep_status set retry_at=now()+make_interval(secs=>least(3600,greatest(0,(patch->>'rate_pause_seconds')::int))) where household_id=hh; end if;
  if patch ? 'day_metrics' then
    update public.sleep_status set day_metrics=patch->'day_metrics',bed_min=(patch->>'bed_min')::int,
      rise_min=(patch->>'rise_min')::int,metrics_at=now(),metrics_error=null where household_id=hh;
  end if;
  if patch ? 'metrics_error' then update public.sleep_status set metrics_error=patch->>'metrics_error' where household_id=hh; end if;
  if patch ? 'history_error' then update public.sleep_status set history_error=patch->>'history_error' where household_id=hh; end if;
  if coalesce((patch->>'history_ok')::boolean,false) then
    update public.sleep_status set history_at=now(),history_error=case when patch ? 'history_error' then patch->>'history_error' else null end where household_id=hh;
  end if;
  -- Retain only the data needed by the rolling budget and reconciliation. No entry/tombstone purge.
  delete from public.cw_requests where household_id=hh and at < now()-interval '25 hours';
  delete from public.sleep_observations where household_id=hh and observed_at < now()-interval '7 days';
  return true;
end $$;

-- Apply an entire derivation/reconciliation decision while holding the household lock.
-- Every existing-row action has an opaque version; if one lost a race, apply none of them.
create function public.cw_apply(hh uuid, token uuid, actions jsonb) returns boolean language plpgsql security definer set search_path = '' as $$
declare s public.sleep_status; a jsonb; e public.entries; kid uuid;
begin
  select * into s from public.sleep_status where household_id=hh for update;
  if s.lease_token is distinct from token or s.lease_until < clock_timestamp() or not s.derive_enabled then return false; end if;
  for a in select * from jsonb_array_elements(actions) loop
    if a ? 'id' then
      select * into e from public.entries where id=(a->>'id')::uuid and household_id=hh and type='sleep' for update;
      if not found or e.updated_at is distinct from (a->>'version')::timestamptz or e.deleted_at is not null
        or coalesce((e.payload->>'timing_locked')::boolean,false) then return false; end if;
    end if;
  end loop;
  select id into kid from public.children where household_id=hh order by created_at limit 1;
  for a in select * from jsonb_array_elements(actions) loop
    if a->>'op'='insert' then
      if exists(select 1 from public.entries where household_id=hh and type='sleep' and
        (source_key=a->>'source_key' or payload->>'suppressed_source_key'=a->>'source_key' or
         (deleted_at is null and ended_at is null and a->>'ended_at' is null))) then continue; end if;
      insert into public.entries(household_id,child_id,type,started_at,ended_at,payload,source_key,via)
        values(hh,kid,'sleep',(a->>'started_at')::timestamptz,(a->>'ended_at')::timestamptz,a->'payload',a->>'source_key','cradlewise')
        on conflict(household_id,source_key) do nothing;
    else
      update public.entries set
        started_at=case when a ? 'started_at' then (a->>'started_at')::timestamptz else started_at end,
        ended_at=case when a ? 'ended_at' then (a->>'ended_at')::timestamptz else ended_at end,
        payload=payload || coalesce(a->'payload','{}'),
        source_key=case when a ? 'source_key' then a->>'source_key' else source_key end
        where id=(a->>'id')::uuid and household_id=hh;
    end if;
  end loop;
  return true;
exception when unique_violation then return false;
end $$;

create function public.monitor_settings(enabled boolean default null, tz text default null) returns jsonb language plpgsql security definer set search_path = '' as $$
declare hh uuid := public.my_household_id(); result jsonb;
begin
  if hh is null then raise exception 'Not in a household' using errcode='42501'; end if;
  if tz is not null then
    if not exists(select 1 from pg_catalog.pg_timezone_names where name=tz) then raise exception 'Unknown timezone'; end if;
    update public.households set timezone=tz where id=hh;
  end if;
  insert into public.sleep_status(household_id) values(hh) on conflict do nothing;
  if enabled is not null then update public.sleep_status set derive_enabled=enabled where household_id=hh; end if;
  select to_jsonb(s) || jsonb_build_object('timezone',h.timezone,'requests_24h',
    (select count(*) from public.cw_requests where household_id=hh and at>now()-interval '24 hours'))
    into result from public.sleep_status s join public.households h on h.id=s.household_id where s.household_id=hh;
  -- Lease token is internal; never disclose it to caregivers.
  return result - 'lease_token';
end $$;
revoke all on function public.cw_lease(uuid), public.cw_admit(uuid,uuid,text), public.cw_finish(uuid,uuid,bigint,jsonb,jsonb), public.cw_apply(uuid,uuid,jsonb), public.monitor_settings(boolean,text) from public,anon,authenticated;
grant execute on function public.cw_lease(uuid), public.cw_admit(uuid,uuid,text), public.cw_finish(uuid,uuid,bigint,jsonb,jsonb), public.cw_apply(uuid,uuid,jsonb) to service_role;
grant execute on function public.monitor_settings(boolean,text) to authenticated;

create or replace function public.set_updated_at() returns trigger language plpgsql set search_path = '' as $$
begin new.updated_at := greatest(clock_timestamp(), old.updated_at + interval '1 microsecond'); return new; end $$;
