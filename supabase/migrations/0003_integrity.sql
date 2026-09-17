-- Integrity constraints the UI cannot be trusted to enforce alone (audit A07, security notes).
-- Safe on the live data as of 2026-09-17: verified zero reversed intervals, zero negative
-- quantities, and every child/caregiver reference already within its household.

-- 1. A child or caregiver referenced by an entry must belong to the entry's household.
alter table children add constraint children_id_household_key unique (id, household_id);
alter table caregivers add constraint caregivers_user_household_key unique (user_id, household_id);

alter table entries
  add constraint entries_child_household_fk foreign key (child_id, household_id) references children (id, household_id),
  add constraint entries_created_by_household_fk foreign key (created_by, household_id) references caregivers (user_id, household_id),
  add constraint entries_updated_by_household_fk foreign key (updated_by, household_id) references caregivers (user_id, household_id);

-- 2. Time and quantity sanity. Payload numbers are optional, so only present values are checked.
alter table entries
  add constraint entries_interval_check check (ended_at is null or ended_at >= started_at),
  add constraint entries_payload_nonnegative check (
    coalesce((payload->>'left_s')::numeric, 0) >= 0
    and coalesce((payload->>'right_s')::numeric, 0) >= 0
    and coalesce((payload->>'breast_milk_ml')::numeric, 0) >= 0
    and coalesce((payload->>'formula_ml')::numeric, 0) >= 0
    and coalesce((payload->>'left_ml')::numeric, 0) >= 0
    and coalesce((payload->>'right_ml')::numeric, 0) >= 0
    and coalesce((payload->>'weight_kg')::numeric, 0) >= 0
    and coalesce((payload->>'height_cm')::numeric, 0) >= 0
    and coalesce((payload->>'head_cm')::numeric, 0) >= 0
  );

-- 3. Clients may not forge attribution: created_by on insert and updated_by on update must be the caller.
drop policy "entries insert" on entries;
create policy "entries insert" on entries for insert
  with check (household_id = my_household_id() and created_by = auth.uid());

drop policy "entries update" on entries;
create policy "entries update" on entries for update
  using (household_id = my_household_id())
  with check (household_id = my_household_id() and updated_by = auth.uid());
