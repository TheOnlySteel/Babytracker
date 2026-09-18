-- Commit separately: PostgreSQL cannot use a newly added enum value in the same transaction.
alter type public.entry_type add value if not exists 'sleep';
