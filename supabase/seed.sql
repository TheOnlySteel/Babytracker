-- Run AFTER 0001_init.sql and AFTER both users exist under Authentication → Users.
-- Replace both placeholder emails, then run in the SQL editor. Safe to re-run: skips what already exists.

do $$
declare
  hh uuid;
  steel uuid;
  dom uuid;
begin
  select id into steel from auth.users where email = 'STEEL_EMAIL_HERE';
  select id into dom   from auth.users where email = 'DOMINIQUE_EMAIL_HERE';
  if steel is null or dom is null then
    raise exception 'Create both users under Authentication → Users first (found steel=%, dominique=%)', steel, dom;
  end if;

  select id into hh from households limit 1;
  if hh is null then
    insert into households (name) values ('Lane') returning id into hh;
    insert into children (household_id, name, sex, birth_date) values (hh, 'Rosalie', 'female', '2026-08-09');
    insert into household_prefs (household_id, prefs) values (hh, '{"formula_brands": ["Enfamil Neuropro", "Good Start Plus"]}');
  end if;
  insert into caregivers (user_id, household_id, display_name) values (steel, hh, 'Steel'), (dom, hh, 'Dominique')
  on conflict (user_id) do nothing;
end $$;

select h.id as household_id, c.id as child_id, c.name, cg.display_name, cg.user_id
from households h join children c on c.household_id = h.id join caregivers cg on cg.household_id = h.id;
