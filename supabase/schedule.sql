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
