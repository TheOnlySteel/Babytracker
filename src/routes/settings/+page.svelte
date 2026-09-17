<script lang="ts">
  import { store } from '$lib/data/store.svelte';
  import { toast } from '$lib/data/toast.svelte';
  import type { Entry } from '$lib/data/types';

  let exporting = $state(false);

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
    const cols = ['id', 'type', 'started_at', 'ended_at', 'note', 'created_by', 'updated_by', 'created_at', 'updated_at', 'deleted_at', 'nara_activity_key'] as const;
    const payloadKeys = ['left_s', 'right_s', 'begin_side', 'end_side', 'manual', 'kinds', 'breast_milk_ml', 'formula_ml', 'formula_brand', 'wet', 'dirty', 'dry', 'texture', 'color', 'blowout', 'rash', 'left_ml', 'right_ml', 'weight_kg', 'height_cm', 'head_cm'];
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

  async function exportAll() {
    exporting = true;
    try {
      const rows = await store.fetchEverything();
      if (!rows) return;
      const stamp = new Date().toISOString().slice(0, 10);
      download(`babytracker-${stamp}.json`, 'application/json', JSON.stringify({ exported_at: new Date().toISOString(), child: store.child, caregivers: store.caregivers, entries: rows }, null, 2));
      download(`babytracker-${stamp}.csv`, 'text/csv', toCsv(rows));
      toast(`Exported ${rows.length} entries`);
    } finally {
      exporting = false;
    }
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
  <div class="card">
    <div class="row col">
      <span class="row-label">Backup</span>
      <span class="muted">Downloads every entry, including soft-deleted ones, as JSON and CSV. Do this now and then; the free database tier has no point-in-time recovery.</span>
      <button class="btn-ghost" onclick={exportAll} disabled={exporting}>{exporting ? 'Exporting…' : 'Export everything'}</button>
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
  main { padding: calc(var(--safe-t) + 24px) 16px calc(var(--tab-h) + var(--safe-b) + 24px); display: flex; flex-direction: column; gap: 16px; }
  h2 { font-size: 30px; padding: 0 4px; }
  .card { background: var(--card); border-radius: var(--radius); overflow: hidden; }
  .card .row:last-child { border-bottom: 0; }
  .col { flex-direction: column; align-items: flex-start; gap: 10px; }
</style>
