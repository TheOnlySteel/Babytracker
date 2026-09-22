# Cradlewatch

Feed, diaper, pump and sleep log for two caregivers, with a bedside pad. Three parts share one Supabase backend:

- **Web app** (`src/`): an installable SvelteKit PWA that replaced the Nara Baby app, imports the full Nara history and syncs between phones in realtime. Deployed at https://lanebabytracker.netlify.app until the Cradlewatch domain is pointed at it.
- **Cradlewise poller** (`supabase/functions/cradlewise-poll/`): a scheduled Edge Function that reads the crib's sleep state, keeps the observation history and, when switched on, derives sleep entries.
- **Cradlewatch pad** (`firmware/Cradlewatch/`): M5Stack Core2 firmware with the logging hub, the crib-state dashboard and the night lamp. It talks only to Supabase and never holds a Cradlewise credential.

## Layout

| Path | What |
| --- | --- |
| `src/`, `static/` | the web app |
| `supabase/migrations/` | schema, applied in order; `schedule.sql` sets up the poller's cron, `seed.sql` the household |
| `supabase/functions/` | the poller and the calendar code it shares with the web app |
| `supabase/tests/` | database tests (PGlite locally, native Postgres 16 in CI) |
| `firmware/Cradlewatch/` | pad firmware; build notes in its README |
| `scripts/` | Nara CSV importer and icon generator |
| `tests/e2e/` | Playwright phone scenarios against a mocked Supabase |
| `docs/` | [setup](docs/setup.md), [rollout](docs/rollout.md), [Cradlewise API notes](docs/cradlewise-data-api.md), [integration plan](docs/integration-plan.md), [original spec](docs/spec.md), [roadmap](docs/ROADMAP.md); `docs/archive/` keeps the superseded review |

## Develop

```sh
npm install
npm run dev        # needs PUBLIC_SUPABASE_URL / PUBLIC_SUPABASE_ANON_KEY in .env
npm test           # unit and PGlite database tests; the Nara acceptance numbers run when data/export*.csv is present
npm run check      # svelte-check
npm run test:e2e   # Playwright
npm run build
```

First-time backend setup is in [docs/setup.md](docs/setup.md). The sleep integration's migration order, gates and rollback are in [docs/rollout.md](docs/rollout.md).
