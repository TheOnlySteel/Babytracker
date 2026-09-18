create schema device;
revoke all on schema device from public,anon,authenticated;
create table public.devices (
  id uuid primary key default gen_random_uuid(), household_id uuid not null references public.households on delete cascade,
  name text not null check(length(name) between 1 and 60), key_hash bytea not null unique,
  boot_mode text not null default 'hub' check(boot_mode in ('hub','dashboard','lamp')),
  last_seen_at timestamptz, revoked_at timestamptz, created_at timestamptz not null default now()
);
create table device.ops (
  device_id uuid not null references public.devices on delete cascade, client_op_id uuid not null,
  op text not null, request_hash bytea not null, result jsonb not null,
  entry_id uuid, created_at timestamptz not null default now(), primary key(device_id,client_op_id)
);
alter table public.devices enable row level security;
alter table device.ops enable row level security;
revoke all on public.devices, device.ops from public,anon,authenticated;
-- The hash and household association are never client-writable (nor client-readable).
grant select(id,household_id,name,boot_mode,last_seen_at,revoked_at,created_at) on public.devices to authenticated;
create policy devices_read on public.devices for select to authenticated using(household_id=public.my_household_id());

create function public.pair_device(name text) returns jsonb language plpgsql security definer set search_path = '' as $$
declare hh uuid := public.my_household_id(); secret text; d public.devices;
begin
  if hh is null then raise exception 'Not in a household' using errcode='42501'; end if;
  -- Two random UUIDs: 244 random bits; token is shown once, only a SHA-256 digest is retained.
  secret := replace(gen_random_uuid()::text || gen_random_uuid()::text,'-','');
  insert into public.devices(household_id,name,key_hash) values(hh,trim(name),sha256(convert_to(secret,'UTF8'))) returning * into d;
  return jsonb_build_object('id',d.id,'name',d.name,'key',secret);
end $$;
create function public.manage_device(device_id uuid, boot_mode text default null, revoke boolean default false) returns boolean language plpgsql security definer set search_path = '' as $$
begin
  update public.devices d set boot_mode=coalesce(manage_device.boot_mode,d.boot_mode),revoked_at=case when revoke then now() else d.revoked_at end
    where d.id=device_id and d.household_id=public.my_household_id();
  return found;
end $$;
create function device.auth() returns public.devices language plpgsql security definer set search_path = '' as $$
declare d public.devices; key text;
begin
  key := coalesce(current_setting('request.headers',true),'{}')::jsonb->>'x-device-key';
  select * into d from public.devices where key_hash=sha256(convert_to(key,'UTF8')) and revoked_at is null for update;
  if not found then raise exception 'Invalid or revoked device' using errcode='42501'; end if;
  update public.devices set last_seen_at=clock_timestamp() where id=d.id;
  return d;
end $$;

create function device.sleep_stats(hh uuid) returns jsonb language sql stable set search_path = '' as $$
with bounds as (
  select (date_trunc('day',now() at time zone timezone) at time zone timezone) as day,
    ((now() at time zone timezone)::date-1)::text as night_key from public.households where id=hh
), sleeps as (
  select e.*, greatest(0,extract(epoch from least(coalesce(e.ended_at,now()),now())-greatest(e.started_at,b.day))) as seconds
  from public.entries e cross join bounds b where e.household_id=hh and e.type='sleep' and e.deleted_at is null
), night as (select s.* from sleeps s,bounds b where s.payload->>'night_key'=b.night_key and s.started_at<now()),
night_ordered as (
  select started_at,least(coalesce(ended_at,now()),now()) as until,
    max(least(coalesce(ended_at,now()),now())) over(order by started_at,id rows between unbounded preceding and 1 preceding) as prior_end from night
), night_grouped as (
  select *,sum(case when prior_end is null or started_at>prior_end then 1 else 0 end) over(order by started_at,until) as grp from night_ordered
), night_spans as (select min(started_at) as start,max(until) as until from night_grouped group by grp)
select jsonb_build_object(
  'rows', coalesce((select jsonb_agg(jsonb_build_object('id',id,'start',started_at,'end',ended_at,'kind',payload->>'kind','source',payload->>'source',
    'place',payload->>'place','duration_s',seconds,'updated_at',updated_at) order by started_at desc) from (select * from sleeps where seconds>0 order by started_at desc limit 32) listed),'[]'),
  'sleep_min',coalesce((select sum(seconds)/60 from sleeps),0),
  'nap_min',coalesce((select sum(seconds)/60 from sleeps where payload->>'kind'='nap'),0),
  'naps',(select count(*) from sleeps,bounds where started_at>=day and started_at<=now() and payload->>'kind'='nap'),
  'longest_nap_min',coalesce((select max(extract(epoch from least(coalesce(ended_at,now()),now())-started_at))/60 from sleeps,bounds where payload->>'kind'='nap' and started_at>=day),0),
  'last_night_min',coalesce((select sum(greatest(0,extract(epoch from until-start)))/60 from night_spans),0),
  'last_night_wakings',greatest(0,(select count(*) from night_spans)-1),
  'awake_in_bed_s',(select day_metrics->'awake_in_bed_s' from public.sleep_status where household_id=hh)
)
$$;

create function public.device_sleep_today() returns jsonb language plpgsql security definer set search_path = '' as $$
declare d public.devices;
begin d:=device.auth(); return device.sleep_stats(d.household_id); end $$;

create function public.device_snapshot(protocol integer) returns jsonb language plpgsql security definer set search_path = '' as $$
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
    'bed_min',s.bed_min,'rise_min',s.rise_min,'tz',tz,'bf',bf,'pump',p,'sleep',sl,
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

create function public.device_op(envelope jsonb) returns jsonb language plpgsql security definer set search_path = '' as $$
declare d public.devices; cached device.ops; hash bytea; op text; op_id uuid; occurred timestamptz; cg uuid; kid uuid;
  target uuid; e public.entries; result jsonb; args jsonb; p jsonb; segs jsonb; side text; left_s int; right_s int; kind public.entry_type;
begin
  -- Authenticate BEFORE idempotency lookup: revocation invalidates replays, too.
  d:=device.auth();
  begin op_id:=(envelope->>'client_op_id')::uuid; exception when others then return jsonb_build_object('outcome','invalid','reason','client_op_id'); end;
  if op_id is null then return jsonb_build_object('outcome','invalid','reason','client_op_id'); end if;
  hash:=sha256(convert_to(envelope::text,'UTF8'));
  select * into cached from device.ops where device_id=d.id and client_op_id=op_id;
  if found then
    if cached.request_hash<>hash then return jsonb_build_object('outcome','invalid','reason','op_id_reuse'); end if;
    return cached.result || case when cached.result->>'outcome'='applied' then '{"outcome":"duplicate"}'::jsonb else '{}'::jsonb end;
  end if;
  op:=envelope->>'op'; args:=coalesce(envelope->'args','{}');
  begin
    if (envelope->>'protocol')::int is distinct from 1 then raise exception 'protocol'; end if;
    occurred:=(envelope->>'occurred_at')::timestamptz; cg:=(envelope->>'caregiver_id')::uuid; target:=(envelope->>'target_id')::uuid;
    -- A one-shot log from a pad whose clock has not synced is still worth keeping: it lands at
    -- receipt time and is marked so the phone shows the time as uncertain. Timers need a real clock.
    if (envelope->>'clock_ok')::boolean is distinct from true then
      if op not in ('bottle','diaper') then raise exception 'clock'; end if;
      occurred:=now();
    elsif occurred is null or occurred>now()+interval '2 minutes' or occurred<now()-interval '7 days' then raise exception 'clock';
    end if;
    if op not in ('bottle','diaper','delete') and occurred<now()-interval '45 seconds' then raise exception 'timer_requires_live_connection'; end if;
    if not exists(select 1 from public.caregivers where user_id=cg and household_id=d.household_id) then raise exception 'caregiver'; end if;
    select id into kid from public.children where household_id=d.household_id order by created_at limit 1;
    if op in ('bf_start','pump_start','sleep_start','bottle','diaper') then
      kind:=case op when 'bf_start' then 'breastfeed' when 'pump_start' then 'pump' when 'sleep_start' then 'sleep' when 'bottle' then 'bottle' else 'diaper' end;
      if op in ('bf_start','pump_start','sleep_start') then
        select * into e from public.entries where household_id=d.household_id and type=kind and ended_at is null and deleted_at is null;
        if found then result:=jsonb_build_object('outcome','conflict','reason','already_running','row',to_jsonb(e)); end if;
      end if;
      if result is null then
        p:='{}';
        if op='bf_start' then
          side:=args->>'side'; if side not in ('left','right') or side is null then raise exception 'side'; end if;
          p:=jsonb_build_object('begin_side',side,'end_side',null,'left_s',0,'right_s',0,'manual',false,'segments',jsonb_build_array(jsonb_build_object('side',side,'start',occurred,'end',null)));
        elsif op='sleep_start' then
          if coalesce(args->>'place','crib') not in ('crib','stroller','car','arms','other') then raise exception 'place'; end if;
          p:=jsonb_build_object('kind','nap','source','manual','place',coalesce(args->>'place','crib'));
        elsif op='bottle' then
          if coalesce(args->>'kind','') not in ('breast_milk','formula') or coalesce((args->>'ml')::numeric,0)<=0 or (args->>'ml')::numeric>1000 then raise exception 'bottle_amount'; end if;
          p:=jsonb_build_object('kinds',jsonb_build_array(args->>'kind'),(args->>'kind')||'_ml',(args->>'ml')::numeric);
        elsif op='diaper' then
          if not(coalesce((args->>'wet')::boolean,false) or coalesce((args->>'dirty')::boolean,false)) then raise exception 'diaper_kind'; end if;
          p:=jsonb_build_object('wet',coalesce((args->>'wet')::boolean,false),'dirty',coalesce((args->>'dirty')::boolean,false),'dry',false,'texture','[]'::jsonb,'color','[]'::jsonb,'rash',false,'blowout',false);
        end if;
        if (envelope->>'clock_ok')::boolean is distinct from true then p:=p||'{"time_uncertain":true}'::jsonb; end if;
        insert into public.entries(household_id,child_id,type,started_at,ended_at,payload,created_by,updated_by,via)
          values(d.household_id,case when kind='pump' then null else kid end,kind,occurred,case when op in ('bottle','diaper') then occurred else null end,p,cg,cg,'core2:'||d.id)
          returning * into e;
      end if;
    elsif op in ('bf_switch','bf_stop','pump_stop','sleep_stop','sleep_dismiss','delete','pump_amount') then
      select * into e from public.entries where id=target and household_id=d.household_id for update;
      if not found then raise exception 'target'; end if;
      if e.updated_at is distinct from (envelope->>'expected_updated_at')::timestamptz or e.deleted_at is not null then
        result:=jsonb_build_object('outcome','conflict','reason','changed','row',to_jsonb(e));
      else
        if occurred<e.started_at then raise exception 'before_start'; end if;
        p:=e.payload;
        if op='delete' then
          if e.via<>'core2:'||d.id then raise exception 'device_did_not_create_entry'; end if;
        elsif op='pump_amount' then
          if e.type<>'pump' or e.ended_at is null or (args->>'total_ml')::numeric is null or (args->>'total_ml')::numeric<0 or (args->>'total_ml')::numeric>2000 then raise exception 'pump_amount'; end if;
          p:=(p-'left_ml'-'right_ml')||jsonb_build_object('total_ml',(args->>'total_ml')::numeric);
        elsif op='sleep_dismiss' then
          if e.type<>'sleep' or p->>'source' is distinct from 'cradlewise' then raise exception 'not_auto_sleep'; end if;
        else
          if e.ended_at is not null then raise exception 'already_stopped'; end if;
          if (op like 'bf_%' and e.type<>'breastfeed') or (op='pump_stop' and e.type<>'pump') or (op='sleep_stop' and e.type<>'sleep') then raise exception 'wrong_timer'; end if;
          if op like 'bf_%' then
            if exists(select 1 from jsonb_array_elements(p->'segments') x where (x->>'start')::timestamptz>occurred or (x->>'end')::timestamptz>occurred) then raise exception 'before_segment'; end if;
            select jsonb_agg(case when x->>'end' is null then x||jsonb_build_object('end',occurred) else x end order by n) into segs from jsonb_array_elements(p->'segments') with ordinality as a(x,n);
            side:=segs->-1->>'side';
            select floor(coalesce(sum(extract(epoch from (x->>'end')::timestamptz-(x->>'start')::timestamptz)) filter(where x->>'side'='left'),0)),
              floor(coalesce(sum(extract(epoch from (x->>'end')::timestamptz-(x->>'start')::timestamptz)) filter(where x->>'side'='right'),0)) into left_s,right_s from jsonb_array_elements(segs) x;
            if op='bf_switch' then segs:=segs||jsonb_build_array(jsonb_build_object('side',case side when 'left' then 'right' else 'left' end,'start',occurred,'end',null)); end if;
            p:=p||jsonb_build_object('segments',segs,'left_s',left_s,'right_s',right_s,'end_side',side);
          elsif op='pump_stop' and args ? 'total_ml' then
            if (args->>'total_ml')::numeric<0 or (args->>'total_ml')::numeric>2000 then raise exception 'pump_amount'; end if;
            p:=(p-'left_ml'-'right_ml')||jsonb_build_object('total_ml',(args->>'total_ml')::numeric);
          elsif op='sleep_stop' then p:=p||'{"timing_locked":true}'::jsonb;
          end if;
        end if;
        update public.entries set payload=p,updated_by=cg,
          ended_at=case when op in ('bf_stop','pump_stop','sleep_stop') then occurred else ended_at end,
          deleted_at=case when op in ('delete','sleep_dismiss') then now() else deleted_at end where id=e.id returning * into e;
      end if;
    else raise exception 'unknown_op'; end if;
    if result is null then result:=jsonb_build_object('outcome','applied','row',to_jsonb(e)); end if;
  exception
    when unique_violation then result:=jsonb_build_object('outcome','conflict','reason','already_running');
    when invalid_text_representation or invalid_datetime_format or datetime_field_overflow or check_violation or raise_exception or not_null_violation or numeric_value_out_of_range then
      result:=jsonb_build_object('outcome','invalid','reason',SQLERRM);
  end;
  insert into device.ops(device_id,client_op_id,op,request_hash,result,entry_id) values(d.id,op_id,coalesce(op,''),hash,result,e.id);
  return result;
end $$;
revoke all on all functions in schema device from public,anon,authenticated;
revoke all on function public.pair_device(text),public.manage_device(uuid,text,boolean),public.device_snapshot(integer),public.device_sleep_today(),public.device_op(jsonb) from public,anon,authenticated;
grant execute on function public.pair_device(text),public.manage_device(uuid,text,boolean) to authenticated;
grant execute on function public.device_snapshot(integer),public.device_sleep_today(),public.device_op(jsonb) to anon;
