-- Extensions, enabled once on the live project (not a migration: PGlite cannot load them). This is
-- what the live migration history calls 0007 "cron_and_net"; that is why the repo has no 0007 file:
--   create extension if not exists pg_cron with schema pg_catalog;
--   create extension if not exists pg_net with schema extensions;
-- Run AFTER deploying cradlewise-poll and stopping the standalone monitors.
-- Vault secrets: cradlewise_poll_url (full Edge Function URL), cradlewise_scheduler_secret.
-- Both this scheduler and the function use the same secret. No secret in source or cron text.
select cron.unschedule(jobid) from cron.job where jobname = 'cradlewise-poll';
select cron.schedule('cradlewise-poll', '30 seconds', $job$
  select net.http_post(
    url := (select decrypted_secret from vault.decrypted_secrets where name='cradlewise_poll_url'),
    headers := jsonb_build_object('Content-Type','application/json','x-scheduler-secret',
      (select decrypted_secret from vault.decrypted_secrets where name='cradlewise_scheduler_secret')),
    body := '{}'::jsonb, timeout_milliseconds := 25000
  );
$job$);

-- Housekeeping, daily at 11:17 UTC (early morning in the household zone). The poller prunes its
-- own tables in cw_finish (cw_requests after 25 h, sleep_observations after 7 days); these two are
-- not pruned by anything else:
--   cron.job_run_details gains one row per run, about 2,900 a day (~87,000 a month) from the
--   30-second job above, and pg_cron never deletes them;
--   device.ops keeps every pad operation for idempotent replay. A pad replays its queue within
--   days (one-shots older than 7 days are refused anyway), so 30 days is ample.
select cron.unschedule(jobid) from cron.job where jobname = 'cradlewatch-prune';
select cron.schedule('cradlewatch-prune', '17 11 * * *', $job$
  delete from cron.job_run_details where end_time < now() - interval '7 days';
  delete from device.ops where created_at < now() - interval '30 days';
$job$);
