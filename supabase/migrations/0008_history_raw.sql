-- Keep the most recent raw /sleep/c-chart response so the reconciliation parser can be checked
-- against what Cradlewise actually sends (its event vocabulary is not documented).
alter table public.sleep_status add column history_raw jsonb;

create or replace function public.cw_finish(hh uuid, token uuid, request_id bigint, observation jsonb default null, patch jsonb default '{}')
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
  if patch ? 'history_raw' then update public.sleep_status set history_raw=patch->'history_raw' where household_id=hh; end if;
  if patch ? 'history_error' then update public.sleep_status set history_error=patch->>'history_error' where household_id=hh; end if;
  if coalesce((patch->>'history_ok')::boolean,false) then
    update public.sleep_status set history_at=now(),history_error=case when patch ? 'history_error' then patch->>'history_error' else null end where household_id=hh;
  end if;
  -- Retain only the data needed by the rolling budget and reconciliation. No entry/tombstone purge.
  delete from public.cw_requests where household_id=hh and at < now()-interval '25 hours';
  delete from public.sleep_observations where household_id=hh and observed_at < now()-interval '7 days';
  return true;
end $$;
