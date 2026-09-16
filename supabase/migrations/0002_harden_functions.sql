-- Advisor follow-ups: pin search_path on the trigger fn; keep my_household_id callable only by signed-in users
-- (RLS policies evaluate it as the invoking role, so `authenticated` must keep EXECUTE).
create or replace function set_updated_at() returns trigger
language plpgsql set search_path = public as $$
begin
  new.updated_at = now();
  return new;
end $$;

revoke execute on function public.my_household_id() from public, anon;
grant execute on function public.my_household_id() to authenticated;
