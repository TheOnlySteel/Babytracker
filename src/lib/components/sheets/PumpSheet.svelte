<script lang="ts">
  import { untrack } from 'svelte';
  import type { Entry, PumpPayload } from '$lib/data/types';
  import { store } from '$lib/data/store.svelte';
  import { closeSheet } from '$lib/data/ui.svelte';
  import Sheet from '$lib/ui/Sheet.svelte';
  import TimeRow from '$lib/ui/TimeRow.svelte';
  import NoteRow from '$lib/ui/NoteRow.svelte';

  let { entry }: { entry?: Entry } = $props();
  const editing = untrack(() => (entry?.type === 'pump' ? entry : undefined));
  const p0 = editing?.payload as PumpPayload | undefined;

  let startedAt = $state(editing ? new Date(editing.started_at) : new Date());
  let minutes = $state<number | null>(
    editing && editing.ended_at ? Math.round((new Date(editing.ended_at).getTime() - new Date(editing.started_at).getTime()) / 60000) : 20
  );
  let left = $state<number | null>(p0?.left_ml ?? null);
  let right = $state<number | null>(p0?.right_ml ?? null);
  let note = $state(editing?.note ?? '');
  let saving = $state(false);

  async function save() {
    const payload: PumpPayload = {};
    if (left != null) payload.left_ml = left;
    if (right != null) payload.right_ml = right;
    const ended = new Date(startedAt.getTime() + (minutes ?? 0) * 60000);
    saving = true;
    try {
      if (editing) await store.update(editing.id, { started_at: startedAt.toISOString(), ended_at: ended.toISOString(), payload, note: note || null }, { undoLabel: 'Updated pump' });
      else await store.insert({ type: 'pump', started_at: startedAt, ended_at: ended, payload, note: note || null }, { undoLabel: `Logged ${(left ?? 0) + (right ?? 0)} mL pump` });
      closeSheet();
    } finally {
      saving = false;
    }
  }
  async function del() {
    if (!editing) return;
    await store.remove(editing.id, 'Pump deleted');
    closeSheet();
  }
  const numVal = (e: Event) => {
    const v = (e.target as HTMLInputElement).value;
    return v === '' ? null : Number(v);
  };
</script>

<Sheet title="Pump" color="var(--pump)" dark onsave={save} {saving}>
  <TimeRow bind:value={startedAt} />
  <label class="row">
    <span class="row-label">Duration</span>
    <span class="amt"><input type="number" inputmode="numeric" value={minutes ?? ''} oninput={(e) => (minutes = numVal(e))} /> min</span>
  </label>
  <label class="row">
    <span class="row-label">Left</span>
    <span class="amt"><input type="number" inputmode="numeric" placeholder="0" value={left ?? ''} oninput={(e) => (left = numVal(e))} /> mL</span>
  </label>
  <label class="row">
    <span class="row-label">Right</span>
    <span class="amt"><input type="number" inputmode="numeric" placeholder="0" value={right ?? ''} oninput={(e) => (right = numVal(e))} /> mL</span>
  </label>
  <div class="row"><span class="row-label">Total</span><span class="row-value">{(left ?? 0) + (right ?? 0)} mL</span></div>
  <NoteRow bind:value={note} />

  {#snippet footer()}
    {#if editing}
      <button class="btn-ghost danger" onclick={del}>Delete</button>
    {/if}
  {/snippet}
</Sheet>

<style>
  .amt { display: inline-flex; align-items: baseline; gap: 6px; }
  .amt input { width: 80px; text-align: right; background: var(--card-2); border: 0; border-radius: 8px; padding: 8px 10px; font-size: 20px; outline: none; }
  .danger { color: var(--danger); border-color: var(--danger); width: 100%; }
</style>
