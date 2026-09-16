# Babytracker

Feed, diaper and pump log for two caregivers. A small installable web app (SvelteKit + Supabase) that replaces the Nara Baby app for the parts we use, imports the full Nara history, and syncs between phones in realtime.

- `baby-tracker-spec.md` — the design spec (screens, data model, import mapping, acceptance test)
- `SETUP.md` — one-time Supabase and Netlify setup
- `supabase/` — schema migration and household seed
- `scripts/import-nara.mjs` — Nara CSV importer (`npm run import:nara -- export.csv`)
- `src/` — the app

```sh
npm install
npm run dev      # needs PUBLIC_SUPABASE_URL / PUBLIC_SUPABASE_ANON_KEY in .env
npm test         # mapper + summary maths, incl. the spec's acceptance numbers when data/export*.csv is present
npm run check    # svelte-check
npm run build
```

Deployed at https://lanebabytracker.netlify.app.
