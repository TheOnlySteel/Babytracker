# Roadmap after the 2026-09-22 audit

The goal is for the family to rely on Cradlewatch by the end of the week of 2026-09-21. The first section is the critical path, in order. The steps that touch production (migrations, deploys, reflashing) are left for a person to run. Everything after that section can wait.

## This week, in order

1. **Merge the audit branch.** Netlify then builds `main`, and the site goes out at https://cradlewatch.com. This build switches the service worker from auto-update to prompt-and-apply. Phones that still run the old worker only pick the change up once the PWA has been fully closed and reopened, so do that twice on each phone. After that, a new build shows a "Reload" toast, and it applies itself when the app goes to the background with no sheet open.
2. **Move the phones to the new origin.** If the home-screen app was installed from `lanebabytracker.netlify.app`, it lives on a different origin from `cradlewatch.com`. Its session and settings do not carry over.
   - Delete the icon.
   - Open https://cradlewatch.com in Safari, choose Add to Home Screen, and sign in once.
   - In Supabase, go to **Authentication → URL Configuration**. Set the Site URL to `https://cradlewatch.com` and keep the netlify.app URL in the redirect allow-list.
3. **Apply migration 0010** (one migration, safe on live data), then re-run the Supabase advisors. The `auth_rls_initplan` warnings should be gone. What should remain are the SECURITY DEFINER warnings on the device and settings RPCs, which are there by design, and the unindexed foreign-key notices.
4. **Redeploy `cradlewise-poll`** with `verify_jwt=false`, exactly as before. The new build:
   - polls a settled crib once a minute, where it used to drift to 90 s;
   - no longer lets a metrics or c-chart timeout pause status polling;
   - no longer re-inserts a dismissed sleep that is 36–48 h old.
5. **Schedule the pruning job:** run only the last block of `supabase/schedule.sql` (`cradlewatch-prune`). Nothing else ever deletes `cron.job_run_details` rows, which grow by about 87k a month.
6. **Confirm no standalone monitor is still powered.** The old direct-polling firmware is gone from the tree. A unit still running it would spend the same Cradlewise quota as the server poller (rollout step 3 was never marked done).
7. **Reflash the paired pad** from `firmware/Cradlewatch/` without erasing flash. Its storage namespace is unchanged, so its queue and settings survive the reflash. Then run the hardware checks in the [firmware README](../firmware/Cradlewatch/README.md) and rollout step 10, with this minimum:
   - TLS against the live chain;
   - a router power-cycle;
   - eight offline logs, then a ninth, which must be refused;
   - UNDO while a log is in flight;
   - a timer started on the phone and stopped on the pad;
   - revocation;
   - an overnight run with the lamp.
8. **Before switching on automatic crib logging** (rollout step 8):
   - Land the reconciliation fix that stops a running sleep from being closed by a missed short wake. It is in progress on this branch; if it is not there, do not switch derivation on.
   - Then turn it on in Settings and compare one full night and a few naps with the Cradlewise app.

## Next fixes, by severity

**Pad**
- Any problem notice, including "Select a caregiver first", disables the timer buttons until someone opens Review. Gate timers on snapshot freshness alone.
- Retry on a parked timer operation cannot succeed, because the server caches every outcome by operation id. Retry should rebuild the operation with a new id and a fresh time, or go.
- Battery life: the loop polls touch at about 500 Hz, and static screens redraw twice a second. This is fine on mains power, but it drains a Core2 battery in hours. Add a daytime dim and slow the idle loop.
- Measure heap over a 72-hour run on the profile build before trusting weeks of uptime.

**Poller (matters once derivation is on)**
- Each poll reads 36 h of observations, about 2–4k rows over several paginated calls, twice a minute. Move this to one SQL function that returns only the current run. Narrowing the window instead would change episode keys and bring back dismissed sleeps.
- `cw_finish` issues up to five UPDATEs per call. Each one is a Realtime broadcast of the full `sleep_status` row to every phone. Collapse them into one.
- `device.sleep_stats` scans every sleep the household has ever had on each pad snapshot. Bound it to the last two days.
- Every pad snapshot writes `devices.last_seen_at`. Write it only when the stored value is more than a minute old.
- The c-chart day start is hardcoded to 08:00 instead of read from the response's `day_start_time`. `parseMetrics`'s nap list is always empty against the live format.

**Web app**
- No offline write queue: a Same-again tap with no signal is lost. The spec's phase 3 calls for a small persisted queue of inserts keyed by a client id.
- Clock times and the home card's today/yesterday use the phone's zone, while History and Summary use the household zone. They disagree when travelling. Route everything through the household zone.
- A sheet's draft is lost if iOS kills the backgrounded PWA. Keep drafts in sessionStorage.
- Manual breastfeeds are saved as starting now and ending in the future. Save them as ending now, with the start back-dated.
- Add a Content-Security-Policy (self, `*.supabase.co` over https and wss, Google Fonts), and test it on the phones.
- Low: CSV export does not escape cells that start with `=`, `+`, `-` or `@`. The sleep sheet's 16-hour guard uses `confirm()`, which the spec rules out.

## Housekeeping

- Rename the remaining outside names:
  - the GitHub repository (`Babytracker` → `Cradlewatch`; GitHub redirects the old URLs);
  - the Supabase project's display name;
  - the Netlify site (`lanebabytracker`). Only its netlify.app subdomain changes; the custom domain is unaffected.
- The repository is public, and the spec, setup notes and Git history name the family. Make it private.
- Enable leaked-password protection in Supabase Auth if the plan allows it.
- The live project is in us-east-1, not Oregon as the setup notes intended. That costs tens of milliseconds per request, which is not worth a migration.
- Backups: the free tier has no point-in-time recovery. Settings → Export everything now and then. A scheduled export would be better.
