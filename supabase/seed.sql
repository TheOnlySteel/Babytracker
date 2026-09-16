-- Run AFTER 0001_init.sql and AFTER both caregivers have signed in at least once
-- (so their rows exist in auth.users). Edit the two emails, then run in the SQL editor.

do $$
declare
  hh uuid;
  steel uuid;
  dom uuid;
begin
  select id into steel from auth.users where email = 'steel640@hotmail.com';
  select id into dom   from auth.users where email = 'DOMINIQUE_EMAIL_HERE';
  if steel is null or dom is null then
    raise exception 'Both caregivers must sign in once before seeding (found steel=%, dominique=%)', steel, dom;
  end if;

  insert into households (name) values ('Lane') returning id into hh;
  insert into children (household_id, name, sex, birth_date) values (hh, 'Rosalie', 'female', '2026-08-09');
  insert into caregivers (user_id, household_id, display_name) values (steel, hh, 'Steel'), (dom, hh, 'Dominique');
  insert into household_prefs (household_id, prefs) values (hh, '{"formula_brands": ["Enfamil Neuropro", "Good Start Plus"]}');
end $$;

select h.id as household_id, c.id as child_id, c.name, cg.display_name, cg.user_id
from households h join children c on c.household_id = h.id join caregivers cg on cg.household_id = h.id;
