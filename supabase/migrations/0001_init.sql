-- Baby tracker schema. Run in the Supabase SQL editor (or `supabase db push`).
-- Everything is scoped to a household; RLS lets a caregiver see only their own household.

create extension if not exists pgcrypto;

create table households (
  id uuid primary key default gen_random_uuid(),
  name text not null default 'Home',
  created_at timestamptz not null default now()
);

create table children (
  id uuid primary key default gen_random_uuid(),
  household_id uuid not null references households on delete cascade,
  name text not null,
  sex text,
  birth_date date not null,
  created_at timestamptz not null default now()
);

create table caregivers (
  user_id uuid primary key references auth.users on delete cascade,
  household_id uuid not null references households on delete cascade,
  display_name text not null,
  created_at timestamptz not null default now()
);

create type entry_type as enum ('breastfeed', 'bottle', 'combo', 'diaper', 'pump', 'growth');

create table entries (
  id uuid primary key default gen_random_uuid(),
  household_id uuid not null references households on delete cascade,
  child_id uuid references children on delete set null,   -- null for pump
  type entry_type not null,
  started_at timestamptz not null,
  ended_at timestamptz,                                    -- null while a timer runs
  payload jsonb not null default '{}'::jsonb,
  note text,
  created_by uuid references caregivers (user_id) on delete set null,
  updated_by uuid references caregivers (user_id) on delete set null,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  deleted_at timestamptz,                                  -- soft delete, purged after 30 days
  nara_activity_key text unique                            -- idempotent re-import
);

create index entries_household_started_idx on entries (household_id, started_at desc) where deleted_at is null;
create index entries_household_type_started_idx on entries (household_id, type, started_at desc) where deleted_at is null;

-- Only one running breastfeed and one running pump per household.
create unique index entries_one_running_breastfeed on entries (household_id)
  where type = 'breastfeed' and ended_at is null and deleted_at is null;
create unique index entries_one_running_pump on entries (household_id)
  where type = 'pump' and ended_at is null and deleted_at is null;

-- Sticky preferences per household (last formula brand, recent amounts, etc.)
create table household_prefs (
  household_id uuid primary key references households on delete cascade,
  prefs jsonb not null default '{}'::jsonb,
  updated_at timestamptz not null default now()
);

-- updated_at maintenance
create or replace function set_updated_at() returns trigger language plpgsql as $$
begin
  new.updated_at = now();
  return new;
end $$;
create trigger entries_set_updated_at before update on entries for each row execute function set_updated_at();
create trigger household_prefs_set_updated_at before update on household_prefs for each row execute function set_updated_at();

-- Helper: the caller's household. `security definer` so it can be used inside policies without recursion.
create or replace function my_household_id() returns uuid
language sql stable security definer set search_path = public as $$
  select household_id from caregivers where user_id = auth.uid()
$$;

-- RLS
alter table households enable row level security;
alter table children enable row level security;
alter table caregivers enable row level security;
alter table entries enable row level security;
alter table household_prefs enable row level security;

create policy "own household" on households for select using (id = my_household_id());
create policy "own household children" on children for select using (household_id = my_household_id());
create policy "own household caregivers" on caregivers for select using (household_id = my_household_id());

create policy "entries select" on entries for select using (household_id = my_household_id());
create policy "entries insert" on entries for insert with check (household_id = my_household_id());
create policy "entries update" on entries for update using (household_id = my_household_id()) with check (household_id = my_household_id());
create policy "entries delete" on entries for delete using (household_id = my_household_id());

create policy "prefs select" on household_prefs for select using (household_id = my_household_id());
create policy "prefs insert" on household_prefs for insert with check (household_id = my_household_id());
create policy "prefs update" on household_prefs for update using (household_id = my_household_id()) with check (household_id = my_household_id());

-- Realtime on entries and prefs. RLS applies to realtime too, so each caregiver only receives their household's rows.
alter publication supabase_realtime add table entries;
alter publication supabase_realtime add table household_prefs;
-- Send full old row on update/delete so clients can reconcile.
alter table entries replica identity full;
