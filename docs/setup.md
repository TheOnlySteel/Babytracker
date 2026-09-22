# Setup

One-time steps to get the tracker live. About 30 minutes. The app is useless until step 3 is done, so do these in order.

These are the steps for a **new Cradlewatch backend**. If Cradlewatch already signs in and saves logs against Supabase, reuse that project and household; do not recreate them. To add the new Cradlewise connection, follow [the rollout guide](docs/rollout.md). A tested implementation does not mean its cloud services have been configured or deployed.

## 1. Supabase project

1. https://supabase.com/dashboard → **New project**. Region: closest to home (the live project ended up in **us-east-1**). Save the database password somewhere; you won't need it day to day.
2. When it finishes provisioning, open **Project Settings → API** and copy:
   - Project URL → `PUBLIC_SUPABASE_URL`
   - `anon` `public` key → `PUBLIC_SUPABASE_ANON_KEY`
   - `service_role` key → `SUPABASE_SERVICE_ROLE_KEY` (import script only; never put this in Netlify or the browser)

## 2. Schema

For the repository's existing dashboard-managed workflow, use **SQL Editor → New query**, then run every file in `supabase/migrations/` in order: `0001_init.sql`, `0002_harden_functions.sql`, `0003_integrity.sql`, `0004_sleep_enum.sql`, `0005_sleep.sql`, and `0006_devices.sql`. Run **one file per committed query**; 0004 must commit before 0005 references the new enum value. Together they create the original tracker schema and the sleep/device integration. Note which files you have applied; dashboard SQL does not register them in CLI migration history. If the project is already managed through CLI migrations, keep that workflow and reconcile the applied history before deploying; do not blindly rerun files or mix deployment methods. See [Supabase's migration guidance](https://supabase.com/docs/guides/deployment/database-migrations).

Applying the schema does not connect Cradlewise. Its token, Edge Function, scheduler, timezone and device pairing are separate steps in the integration handoff. Automatic sleep derivation starts off.

## 3. Auth: two accounts with passwords, no open signup

The app signs in with email and password. Each phone signs in once and stays signed in, so you'll type the password about twice in the life of the app. No email is ever sent, which matters: Supabase's free built-in mailer only delivers to members of your Supabase organization, and on new free projects the email templates can't be customised without wiring up your own SMTP provider. Passwords sidestep all of it.

In the sidebar click **Authentication**.

1. **Sign In / Providers → Email**: leave it enabled. Turn **off** "Allow new users to sign up" so nobody but the two of you can make an account.
2. **Users → Add user → Create new user**: enter your email, a password, and tick **Auto Confirm User**. Repeat for Dominique. Pick passwords you can type on a phone; a password manager entry each is sensible.
3. **URL Configuration → Site URL**: `https://cradlewatch.com`.

Forgotten password later: **Authentication → Users → ⋯ → Send password recovery** needs email and won't work here; instead use **⋯ → Reset password** (or delete and re-create the user, then re-run the caregiver insert from `seed.sql` for that one row).

## 4. Household seed

Open `supabase/seed.sql`, replace both placeholder emails, paste into the SQL Editor, **Run**. It creates the household, Rosalie, both caregiver rows, and the default formula brands. The final `select` shows what it made.

## 5. Netlify

The site `lanebabytracker` already exists, deploys from `main` and serves https://cradlewatch.com.

1. **Site configuration → Environment variables** → add `PUBLIC_SUPABASE_URL` and `PUBLIC_SUPABASE_ANON_KEY`.
2. Build settings are in `netlify.toml` (build `npm run build`, publish `build`, Node 22, SPA redirect). Nothing to set in the UI.
3. Merge the branch to `main` and let it deploy. Open the URL on your phone and sign in with the email and password from step 3.

## 6. Import the Nara history

On your Mac, in the repo:

```sh
cp .env.example .env      # then fill in PUBLIC_SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY
npm install
npm run import:nara -- path/to/export_narababy_rosalie_YYYYMMDD.csv
```

Add `--dry-run` first to see the mapping without writing. By default the script only inserts rows whose Nara key is new; rows already in the database are left exactly as they are, so anything you edited in the app survives a re-import. Pass `--overwrite` if Nara should win (for example if you fixed a row in Nara rather than here). Any row it can't map cleanly is listed and nothing is written. Re-run it once more right before you stop logging in Nara.

The unit tests reproduce the spec's acceptance numbers against the real export when it sits in `data/` (gitignored):

```sh
npm test
```

## 7. Install on the phones

Safari → share sheet → **Add to Home Screen**. The icon launches full-screen and keeps its session. Do it on both phones; each signs in once with its own email and password.

## Later

- **Realtime** is on by default for `entries` and `household_prefs`; nothing to enable in the dashboard.
- **Backups**: the free tier has no point-in-time recovery. **Account → Export everything** downloads all entries as JSON and CSV; do it now and then. To restore into a fresh project, run the migrations and seed, then load the JSON with a short script or ask for one.
- **Repository visibility**: the repo is public. The spec and this file name the family; the seed no longer carries an email and the reference screenshots were removed from the current tree, but both remain in Git history. Making the repository private on GitHub (Settings → General → Danger Zone) is the one-click way to close that.
- **Soft deletes** live in `entries.deleted_at`. Nothing purges them yet; 800 rows a month is nothing.
