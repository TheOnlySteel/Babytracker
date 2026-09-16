#!/usr/bin/env node
// Import a Nara Baby CSV export into Supabase. Re-runnable: upserts on nara_activity_key.
//
//   npm run import:nara -- data/export_narababy_rosalie_20260916.csv
//
// Requires in .env: PUBLIC_SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY.
// Resolves household, child and caregivers from the database (run supabase/seed.sql first).

import { readFileSync } from 'node:fs';
import Papa from 'papaparse';
import { createClient } from '@supabase/supabase-js';
import 'dotenv/config';
import { mapRow } from './nara-map.mjs';

const file = process.argv[2];
if (!file) {
  console.error('usage: node scripts/import-nara.mjs <export.csv> [--dry-run]');
  process.exit(1);
}
const dryRun = process.argv.includes('--dry-run');

const url = process.env.PUBLIC_SUPABASE_URL;
const key = process.env.SUPABASE_SERVICE_ROLE_KEY;
if (!dryRun && (!url || !key)) {
  console.error('Set PUBLIC_SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY in .env');
  process.exit(1);
}

const csv = readFileSync(file, 'utf8').replace(/^﻿/, '');
const { data: rows, errors } = Papa.parse(csv, { header: true, skipEmptyLines: true });
// Nara's trailing Profile row is one field short; tolerate field-count mismatches on rows we skip anyway.
const IN_SCOPE = new Set(['Breastfeed', 'Bottle Feed', 'Combo Feed', 'Diaper', 'Pump', 'Growth']);
const fatal = errors.filter((e) => !(e.type === 'FieldMismatch' && e.row != null && !IN_SCOPE.has(rows[e.row]?.Type)));
if (fatal.length) {
  console.error(fatal);
  process.exit(1);
}

let ctx;
if (dryRun) {
  ctx = {
    household_id: '00000000-0000-0000-0000-000000000000',
    child_id: '00000000-0000-0000-0000-000000000001',
    caregivers: { Steel: '00000000-0000-0000-0000-000000000002', Dominique: '00000000-0000-0000-0000-000000000003' }
  };
} else {
  const sb = createClient(url, key, { auth: { persistSession: false } });
  const { data: children, error: e1 } = await sb.from('children').select('id, household_id, name');
  if (e1) throw e1;
  if (!children || children.length !== 1) throw new Error(`Expected exactly one child, found ${children?.length ?? 0}. Run supabase/seed.sql first.`);
  const child = children[0];
  const { data: cgs, error: e2 } = await sb.from('caregivers').select('user_id, display_name').eq('household_id', child.household_id);
  if (e2) throw e2;
  ctx = {
    household_id: child.household_id,
    child_id: child.id,
    caregivers: Object.fromEntries(cgs.map((c) => [c.display_name, c.user_id]))
  };
  ctx.sb = sb;
}

const entries = [];
const skipped = {};
for (const row of rows) {
  const e = mapRow(row, ctx);
  if (e) entries.push(e);
  else skipped[row.Type] = (skipped[row.Type] ?? 0) + 1;
}
const counts = entries.reduce((a, e) => ((a[e.type] = (a[e.type] ?? 0) + 1), a), {});
console.log(`Mapped ${entries.length} entries:`, counts);
console.log('Skipped out-of-scope rows:', skipped);

if (dryRun) {
  console.log(JSON.stringify(entries.slice(0, 3), null, 2));
  process.exit(0);
}

const BATCH = 200;
for (let i = 0; i < entries.length; i += BATCH) {
  const batch = entries.slice(i, i + BATCH);
  const { error } = await ctx.sb.from('entries').upsert(batch, { onConflict: 'nara_activity_key' });
  if (error) {
    console.error(`Batch ${i / BATCH} failed:`, error);
    process.exit(1);
  }
  console.log(`Upserted ${Math.min(i + BATCH, entries.length)}/${entries.length}`);
}
console.log('Done.');
