-- The pad shows the child's name as its hub title. Same function as 0006 plus `child_name`.
create or replace function public.device_snapshot(protocol integer) returns jsonb language plpgsql security definer set search_path = '' as $$
declare d public.devices; s public.sleep_status; tz text; day timestamptz; bf jsonb; p jsonb; sl jsonb; stats jsonb;
  settled jsonb; bed timestamptz; rise_at timestamptz;
begin
  d:=device.auth();
  if protocol is distinct from 1 then return jsonb_build_object('protocol',1,'error','update_firmware'); end if;
  select * into s from public.sleep_status where household_id=d.household_id;
  select timezone into tz from public.households where id=d.household_id;
  day:=date_trunc('day',now() at time zone tz) at time zone tz;
  select jsonb_build_object('id',e.id,'open_since',e.started_at,'updated_at',e.updated_at,
    'side',(select x->>'side' from jsonb_array_elements(coalesce(e.payload->'segments','[]')) x where x->>'end' is null limit 1),
    'left_s',coalesce(t.l,0),'right_s',coalesce(t.r,0),'elapsed_s',coalesce(t.l,0)+coalesce(t.r,0)) into bf
    from public.entries e cross join lateral (
      select floor(sum(greatest(0,extract(epoch from coalesce((x->>'end')::timestamptz,now())-(x->>'start')::timestamptz))) filter(where x->>'side'='left')) l,
        floor(sum(greatest(0,extract(epoch from coalesce((x->>'end')::timestamptz,now())-(x->>'start')::timestamptz))) filter(where x->>'side'='right')) r
      from jsonb_array_elements(coalesce(e.payload->'segments','[]')) x
    ) t where e.household_id=d.household_id and e.type='breastfeed' and e.ended_at is null and e.deleted_at is null;
  select jsonb_build_object('id',id,'open_since',started_at,'updated_at',updated_at) into p from public.entries
    where household_id=d.household_id and type='pump' and ended_at is null and deleted_at is null;
  select jsonb_build_object('id',id,'open_since',started_at,'updated_at',updated_at,'source',payload->>'source',
    'timing_locked',coalesce((payload->>'timing_locked')::boolean,false)) into sl from public.entries
    where household_id=d.household_id and type='sleep' and ended_at is null and deleted_at is null;
  stats:=device.sleep_stats(d.household_id);
  bed:=(date_trunc('day',now() at time zone tz)+make_interval(mins=>coalesce(s.bed_min,1200))) at time zone tz;
  if bed>now() then bed:=((bed at time zone tz)-interval '1 day') at time zone tz; end if;
  rise_at:=(date_trunc('day',bed at time zone tz)+make_interval(mins=>coalesce(s.rise_min,480))) at time zone tz;
  if rise_at<=bed then rise_at:=((rise_at at time zone tz)+interval '1 day') at time zone tz; end if;
  -- Settled measures observed crib states only. Missing intervals are excluded, not called asleep.
  with obs as (
    select status,observed_at,lead(observed_at) over(order by observed_at) next_at,lag(status) over(order by observed_at) prior
    from public.sleep_observations where household_id=d.household_id and observed_at>=bed and observed_at<=least(now(),rise_at)
  ), spans as (
    select *,extract(epoch from next_at-observed_at) seconds from obs
    where next_at-observed_at<=interval '4 minutes'
  ) select case when sum(seconds)>=2700 then jsonb_build_object(
    'settled_pct',round(100*coalesce(sum(seconds) filter(where status in ('sleeping','stirring')),0)/nullif(sum(seconds),0)),
    'settled_asleep_min',coalesce(sum(seconds) filter(where status in ('sleeping','stirring')),0)/60,
    'settled_awake_min',coalesce(sum(seconds) filter(where status in ('awake','crying','away')),0)/60,
    'settled_wakings',count(*) filter(where status in ('awake','crying') and prior in ('sleeping','stirring')))
    else '{}'::jsonb end into settled from spans where status<>'unknown';
  return jsonb_build_object('protocol',1,'snapshot_at',clock_timestamp(),'source_status',coalesce(s.status,'unknown'),
    'source_since',s.since,'source_observed_at',s.observed_at,'source_error',s.source_error,'source_bounce',s.bounce,'source_music',s.music,
    'bed_min',s.bed_min,'rise_min',s.rise_min,'tz',tz,
    'child_name',(select name from public.children where household_id=d.household_id order by created_at limit 1),'bf',bf,'pump',p,'sleep',sl,
    'settled_pct',null,'settled_asleep_min',null,'settled_awake_min',null,'settled_wakings',null,
    'last_feed',(select jsonb_build_object('at',started_at,'kind',type,'ml',coalesce((payload->>'breast_milk_ml')::numeric,0)+coalesce((payload->>'formula_ml')::numeric,0))
      from public.entries where household_id=d.household_id and type in ('breastfeed','bottle','combo') and deleted_at is null order by started_at desc limit 1),
    'last_diaper',(select jsonb_build_object('at',started_at,'kind',case when (payload->>'dirty')::boolean and (payload->>'wet')::boolean then 'both' when (payload->>'dirty')::boolean then 'dirty' else 'wet' end)
      from public.entries where household_id=d.household_id and type='diaper' and deleted_at is null order by started_at desc limit 1),
    'last_pump',(select jsonb_build_object('at',started_at,'ml',coalesce((payload->>'total_ml')::numeric,coalesce((payload->>'left_ml')::numeric,0)+coalesce((payload->>'right_ml')::numeric,0)))
      from public.entries where household_id=d.household_id and type='pump' and deleted_at is null and ended_at is not null order by started_at desc limit 1),
    'today',(stats - 'rows') || jsonb_build_object('feeds',(select count(*) from public.entries where household_id=d.household_id and type in ('bottle','breastfeed','combo') and started_at>=day and deleted_at is null),
      'diapers',(select count(*) from public.entries where household_id=d.household_id and type='diaper' and started_at>=day and deleted_at is null)),
    'caregivers',(select jsonb_agg(jsonb_build_object('id',user_id,'name',display_name) order by created_at) from public.caregivers where household_id=d.household_id),
    'bottle_default_ml',coalesce((select coalesce((payload->>'breast_milk_ml')::numeric,0)+coalesce((payload->>'formula_ml')::numeric,0) from public.entries
      where household_id=d.household_id and type='bottle' and deleted_at is null order by started_at desc limit 1),60),'boot_mode',d.boot_mode) || coalesce(settled,'{}');
end $$;
