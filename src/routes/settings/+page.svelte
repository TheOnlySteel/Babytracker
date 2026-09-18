<script lang="ts">
  import MonitorSettings from '$lib/components/MonitorSettings.svelte';
  import { store } from '$lib/data/store.svelte';
  import { toast } from '$lib/data/toast.svelte';
  import type { Entry } from '$lib/data/types';
  import { entryLabel } from '$lib/data/derive';
  import { fmtWhen } from '$lib/data/format';

  let exporting = $state(false);
  let deleted = $state<Entry[] | null>(null);
  let loadingDeleted = $state(false);

  function download(name: string, mime: string, body: string) {
    const url = URL.createObjectURL(new Blob([body], { type: mime }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  const csvCell = (v: unknown) => {
    const s = v == null ? '' : String(v);
    return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
  };

  function toCsv(rows: Entry[]): string {
    const cols = ['id', 'type', 'started_at', 'ended_at', 'note', 'created_by', 'updated_by', 'created_at', 'updated_at', 'deleted_at', 'nara_activity_key', 'source_key', 'via'] as const;
    const payloadKeys = ['left_s', 'right_s', 'begin_side', 'end_side', 'manual', 'kinds', 'breast_milk_ml', 'formula_ml', 'formula_brand', 'wet', 'dirty', 'dry', 'texture', 'color', 'blowout', 'rash', 'left_ml', 'right_ml', 'weight_kg', 'height_cm', 'head_cm', 'total_ml', 'kind', 'source', 'place', 'night_key', 'timing_locked', 'provisional', 'uncertain_end'];
    const who = (id: string | null) => store.caregivers.find((c) => c.user_id === id)?.display_name ?? id ?? '';
    const head = [...cols, ...payloadKeys].join(',');
    const lines = rows.map((r) => {
      const p = r.payload as Record<string, unknown>;
      const base = cols.map((c) => (c === 'created_by' || c === 'updated_by' ? who(r[c]) : r[c]));
      const extra = payloadKeys.map((k) => (Array.isArray(p[k]) ? (p[k] as unknown[]).join(' ') : p[k]));
      return [...base, ...extra].map(csvCell).join(',');
    });
    return [head, ...lines].join('\n');
  }

  async function exportAll(format: 'json' | 'csv') {
    exporting = true;
    try {
      const rows = await store.fetchEverything();
      if (!rows) return;
      const stamp = new Date().toISOString().slice(0, 10);
      // One file per tap: iOS Safari only reliably delivers a single download per user gesture.
      if (format === 'json') download(`babytracker-${stamp}.json`, 'application/json', JSON.stringify({ exported_at: new Date().toISOString(), child: store.child, caregivers: store.caregivers, entries: rows }, null, 2));
      else download(`babytracker-${stamp}.csv`, 'text/csv', toCsv(rows));
      toast(`Exported ${rows.length} entries as ${format.toUpperCase()}`);
    } finally {
      exporting = false;
    }
  }

  async function showDeleted() {
    loadingDeleted = true;
    deleted = await store.fetchDeleted(30);
    loadingDeleted = false;
  }
  async function restore(e: Entry) {
    if (await store.restore(e)) deleted = (deleted ?? []).filter((d) => d.id !== e.id);
  }
</script>

<main>
  <h2>Account</h2>
  <div class="card">
    <div class="row"><span class="row-label">Signed in as</span><span class="row-value">{store.caregiver?.display_name}</span></div>
    <div class="row"><span class="row-label">Email</span><span class="row-value muted">{store.session?.user.email}</span></div>
    <div class="row"><span class="row-label">Household</span><span class="row-value muted">{store.caregivers.map((c) => c.display_name).join(', ')}</span></div>
    <div class="row"><span class="row-label">Child</span><span class="row-value">{store.child?.name} · born {store.child?.birth_date}</span></div>
    <div class="row">
      <span class="row-label">Sync</span>
      <span class="row-value" class:muted={store.realtime !== 'live'}>{store.realtime === 'live' ? 'Live' : store.realtime === 'offline' ? 'Reconnecting…' : 'Connecting…'}</span>
    </div>
  </div>

  <MonitorSettings/>
  <div class="card">
    <div class="row col">
      <span class="row-label">Recently deleted</span>
      <span class="muted">Anything deleted in the last 30 days can be put back.</span>
      {#if deleted === null}
        <button class="btn-ghost" onclick={showDeleted} disabled={loadingDeleted}>{loadingDeleted ? 'Loading…' : 'Show deleted entries'}</button>
      {:else if deleted.length === 0}
        <span class="muted">Nothing deleted in the last 30 days.</span>
      {/if}
    </div>
    {#if deleted?.length}
      {#each deleted as e (e.id)}
        <div class="row del">
          <div class="what">
            <div>{fmtWhen(e.started_at)} · {entryLabel(e)}</div>
            <div class="muted small">deleted {fmtWhen(e.deleted_at!)}</div>
          </div>
          <button class="btn-ghost" onclick={() => restore(e)}>Restore</button>
        </div>
      {/each}
    {/if}
  </div>

  <div class="card">
    <div class="row col">
      <span class="row-label">Backup</span>
      <span class="muted">Downloads every entry, including deleted ones. Do this now and then; the free database tier has no point-in-time recovery.</span>
      <div class="pair">
        <button class="btn-ghost" onclick={() => exportAll('json')} disabled={exporting}>Export JSON</button>
        <button class="btn-ghost" onclick={() => exportAll('csv')} disabled={exporting}>Export CSV</button>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="row col">
      <span class="row-label">Install</span>
      <span class="muted">On iPhone: Share → Add to Home Screen. The app then opens full-screen and stays signed in.</span>
    </div>
  </div>
  <button class="btn-ghost" onclick={() => store.signOut()}>Sign out</button>
</main>

<style>
  main { padding: 24px 16px calc(var(--tab-h) + var(--safe-b) + 24px); display: flex; flex-direction: column; gap: 16px; }
  h2 { font-size: 30px; padding: 0 4px; }
  .card { background: var(--card); border-radius: var(--radius); overflow: hidden; }
  .card .row:last-child { border-bottom: 0; }
  .col { flex-direction: column; align-items: flex-start; gap: 10px; }
  .pair { display: flex; gap: 10px; }
  .del { gap: 12px; }
  .what { flex: 1; min-width: 0; font-size: 16px; }
  .small { font-size: 13px; }
</style>
