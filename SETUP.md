# Setup

One-time steps to get the tracker live. About 30 minutes. The app is useless until step 3 is done, so do these in order.

## 1. Supabase project

1. https://supabase.com/dashboard → **New project**. Region: **West US (Oregon)** (closest to Vancouver). Save the database password somewhere; you won't need it day to day.
2. When it finishes provisioning, open **Project Settings → API** and copy:
   - Project URL → `PUBLIC_SUPABASE_URL`
   - `anon` `public` key → `PUBLIC_SUPABASE_ANON_KEY`
   - `service_role` key → `SUPABASE_SERVICE_ROLE_KEY` (import script only; never put this in Netlify or the browser)

## 2. Schema

**SQL Editor → New query**, paste the whole of `supabase/migrations/0001_init.sql`, **Run**. It creates the tables, row-level security, the one-running-timer indexes, and adds `entries` to the realtime publication.

## 3. Auth: email codes, no open signup

The app signs in with a 6-digit code emailed to you. Codes work inside an installed PWA; magic links open in Safari instead and lose the session, so we avoid them.

1. **Authentication → Providers → Email**: keep it enabled. Turn **off** "Confirm email" (you'll create the users yourself).
2. **Authentication → Sign In / Providers → "Allow new users to sign up"**: turn **off**. Nobody but the two of you should have an account.
3. **Authentication → Email Templates → Magic Link**: replace the body with something like:
   ```html
   <h2>Your sign-in code</h2>
   <p style="font-size:32px;letter-spacing:4px"><strong>{{ .Token }}</strong></p>
   <p>Enter this in the Rosalie app. It expires in an hour.</p>
   ```
   `{{ .Token }}` is what turns the magic-link email into a code email.
4. **Authentication → Users → Add user → Create new user** twice: your email and Dominique's, with **Auto Confirm User** on. Passwords are irrelevant; pick anything.
5. **Authentication → URL Configuration → Site URL**: `https://lanebabytracker.netlify.app`.

Heads-up: Supabase's built-in mailer is rate-limited to a handful of emails per hour. You sign in once per device and stay signed in, so this is fine in practice. If it ever bites, wire a custom SMTP under **Project Settings → Auth → SMTP** (Resend's free tier works).

## 4. Household seed

Open `supabase/seed.sql`, replace `DOMINIQUE_EMAIL_HERE` with her email (yours is already in), paste into the SQL Editor, **Run**. It creates the household, Rosalie, both caregiver rows, and the default formula brands. The final `select` shows what it made.

## 5. Netlify

The site `lanebabytracker` already exists and deploys from `main`.

1. **Site configuration → Environment variables** → add `PUBLIC_SUPABASE_URL` and `PUBLIC_SUPABASE_ANON_KEY`.
2. Build settings are in `netlify.toml` (build `npm run build`, publish `build`, Node 22, SPA redirect). Nothing to set in the UI.
3. Merge the branch to `main` and let it deploy. Open the URL on your phone, sign in with the code flow.

## 6. Import the Nara history

On your Mac, in the repo:

```sh
cp .env.example .env      # then fill in PUBLIC_SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY
npm install
npm run import:nara -- path/to/export_narababy_rosalie_YYYYMMDD.csv
```

Add `--dry-run` first to see the mapping without writing. The script upserts on Nara's `_activityKey`, so re-running with a newer export adds the new rows and leaves the rest untouched. Re-run it once more right before you stop logging in Nara.

The unit tests reproduce the spec's acceptance numbers against the real export when it sits in `data/` (gitignored):

```sh
npm test
```

## 7. Install on the phones

Safari → share sheet → **Add to Home Screen**. The icon launches full-screen and keeps its session. Do it on both phones; each signs in once with its own email.

## Later

- **Realtime** is on by default for `entries` and `household_prefs`; nothing to enable in the dashboard.
- **Backups**: the free tier has no point-in-time recovery. Export a CSV from the Table Editor now and then if you care.
- **Soft deletes** live in `entries.deleted_at`. Nothing purges them yet; 800 rows a month is nothing.
