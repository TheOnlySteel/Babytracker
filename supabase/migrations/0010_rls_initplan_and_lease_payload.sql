-- Hardening after the 2026-09-22 backend audit. Safe on the live data: no table is rewritten and
-- no row changes. Policies are altered in place (same names, same roles, same meaning); functions
-- are replaced with identical signatures, so their existing grants are kept. Apply as one
-- migration after 0009; the deployed poller and web app work unchanged before and after it.
-- (There is no 0007 file: 0007 "cron_and_net" enabled pg_cron and pg_net on the live project and is
-- kept as the comment at the top of supabase/schedule.sql, because PGlite cannot load them.)

-- 1. The RLS helper resolves nothing through search_path. It was `public`, which a session could
--    shadow with a temporary relation; an empty path with qualified names cannot be shadowed.
create or replace function public.my_household_id() returns uuid
language sql stable security definer set search_path = '' as $$
  select household_id from public.caregivers where user_id = auth.uid()
$$;

-- 2. Evaluate the caller's identity once per statement instead of once per row (advisor
--    auth_rls_initplan). my_household_id() is SECURITY DEFINER and so never inlined: without the
--    sub-select every scanned row paid a function call and a caregivers lookup.
alter policy "own household" on public.households using (id = (select public.my_household_id()));
alter policy "own household children" on public.children using (household_id = (select public.my_household_id()));
alter policy "own household caregivers" on public.caregivers using (household_id = (select public.my_household_id()));
alter policy "entries select" on public.entries using (household_id = (select public.my_household_id()));
alter policy "entries insert" on public.entries
  with check (household_id = (select public.my_household_id()) and created_by = (select auth.uid()));
alter policy "entries update" on public.entries
  using (household_id = (select public.my_household_id()))
  with check (household_id = (select public.my_household_id()) and updated_by = (select auth.uid()));
alter policy "entries delete" on public.entries using (household_id = (select public.my_household_id()));
alter policy "prefs select" on public.household_prefs using (household_id = (select public.my_household_id()));
alter policy "prefs insert" on public.household_prefs with check (household_id = (select public.my_household_id()));
alter policy "prefs update" on public.household_prefs
  using (household_id = (select public.my_household_id()))
  with check (household_id = (select public.my_household_id()));
alter policy sleep_observations_read on public.sleep_observations using (household_id = (select public.my_household_id()));
alter policy sleep_status_read on public.sleep_status using (household_id = (select public.my_household_id()));
alter policy devices_read on public.devices using (household_id = (select public.my_household_id()));

-- 3. Nothing reaches the base tables as anon: the pad uses SECURITY DEFINER RPCs and the web app
--    reads only after sign-in. Until now anon's default Data API grants were stopped only because
--    anon cannot execute my_household_id(). TRUNCATE, REFERENCES and TRIGGER are never needed by
--    either API role (TRUNCATE bypasses RLS). Revoking what was never granted is a no-op.
revoke all on public.households, public.children, public.caregivers, public.entries, public.household_prefs from anon;
revoke truncate, references, trigger on all tables in schema public from anon, authenticated;

-- 4. The lease no longer ships the stored raw c-chart and day-metrics bodies to the poller on
--    every 30 s tick (it reads neither), and caregivers' monitor_settings no longer carries the raw
--    c-chart (the web app does not read it; it stays in sleep_status.history_raw for inspection).
create or replace function public.cw_lease(hh uuid) returns jsonb language plpgsql security definer set search_path = '' as $$
declare s public.sleep_status;
begin
  insert into public.sleep_status(household_id) values(hh) on conflict do nothing;
  update public.sleep_status set lease_token = gen_random_uuid(), lease_until = clock_timestamp() + interval '25 seconds'
    where household_id = hh and (lease_until is null or lease_until < clock_timestamp())
    returning * into s;
  if not found then return null; end if;
  return (to_jsonb(s) - 'history_raw' - 'day_metrics')
    || jsonb_build_object('timezone', (select timezone from public.households where id=hh));
end $$;

create or replace function public.monitor_settings(enabled boolean default null, tz text default null) returns jsonb language plpgsql security definer set search_path = '' as $$
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
  return result - 'lease_token' - 'history_raw';
end $$;
