<script lang="ts">
  import { untrack } from 'svelte';
  import type { Entry, PumpPayload } from '$lib/data/types';
  import { store, ConflictError } from '$lib/data/store.svelte';
  import { closeSheet, openSheet } from '$lib/data/ui.svelte';
  import { nonNegative } from '$lib/data/validate';
  import Sheet from '$lib/ui/Sheet.svelte';
  import ConflictBar from '$lib/ui/ConflictBar.svelte';
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
  let err = $state('');
  let version = $state(editing?.updated_at);
  let conflict = $state<Entry | null>(null);
  const snapshot = () => JSON.stringify([startedAt.getTime(), minutes, left, right, note]);
  const initial = snapshot();
  const dirty = $derived(snapshot() !== initial);

  async function save() {
    err = '';
    const m = nonNegative(minutes, 'Duration', { max: 24 * 60 });
    const l = nonNegative(left, 'Left', { max: 1000 });
    const r = nonNegative(right, 'Right', { max: 1000 });
    const bad = m.error ?? l.error ?? r.error;
    if (bad) return void (err = bad);
    // A pump with no volume is allowed (a dry session is still a session), but it must be deliberate:
    // both fields empty is fine, a typed 0 is fine, only negatives and NaN are rejected above.
    const payload: PumpPayload = {};
    if (l.value != null) payload.left_ml = l.value;
    if (r.value != null) payload.right_ml = r.value;
    const ended = new Date(startedAt.getTime() + (m.value ?? 0) * 60000);
    saving = true;
    try {
      if (editing) await store.update(editing.id, { started_at: startedAt.toISOString(), ended_at: ended.toISOString(), payload, note: note || null }, { undoLabel: 'Updated pump', expectedUpdatedAt: version });
      else await store.insert({ type: 'pump', started_at: startedAt, ended_at: ended, payload, note: note || null }, { undoLabel: `Logged ${(l.value ?? 0) + (r.value ?? 0)} mL pump` });
      closeSheet();
    } catch (e) {
      if (e instanceof ConflictError && e.latest) conflict = e.latest;
    } finally {
      saving = false;
    }
  }
  async function del() {
    if (!editing) return;
    await store.remove(editing.id, 'Pump deleted', version);
    closeSheet();
  }
  const numVal = (e: Event) => {
    err = '';
    const v = (e.target as HTMLInputElement).value;
    return v === '' ? null : Number(v);
  };
</script>

<Sheet title="Pump" color="var(--pump)" dark onsave={save} {saving} {dirty}>
  {#if conflict}
    <ConflictBar latest={conflict} onUseTheirs={() => openSheet('pump', conflict!)} onKeepMine={() => { version = conflict!.updated_at; conflict = null; save(); }} />
  {/if}
  <TimeRow bind:value={startedAt} />
  <label class="row">
    <span class="row-label">Duration</span>
    <span class="amt"><input type="number" inputmode="numeric" min="0" value={minutes ?? ''} oninput={(e) => (minutes = numVal(e))} /> min</span>
  </label>
  <label class="row">
    <span class="row-label">Left</span>
    <span class="amt"><input type="number" inputmode="numeric" min="0" placeholder="0" value={left ?? ''} oninput={(e) => (left = numVal(e))} /> mL</span>
  </label>
  <label class="row">
    <span class="row-label">Right</span>
    <span class="amt"><input type="number" inputmode="numeric" min="0" placeholder="0" value={right ?? ''} oninput={(e) => (right = numVal(e))} /> mL</span>
  </label>
  <div class="row"><span class="row-label">Total</span><span class="row-value">{Math.max(0, left ?? 0) + Math.max(0, right ?? 0)} mL</span></div>
  {#if err}<p class="err" role="alert">{err}</p>{/if}
  <NoteRow bind:value={note} />

  {#snippet footer()}
    {#if editing}
      <button class="btn-link danger" onclick={del} disabled={saving}>Delete</button>
    {/if}
  {/snippet}
</Sheet>

<style>
  .amt { display: inline-flex; align-items: baseline; gap: 6px; }
  .amt input { width: 80px; text-align: right; background: var(--card-2); border: 0; border-radius: 8px; padding: 8px 10px; font-size: 20px; outline: none; }
  .err { margin: 0; padding: 8px 20px; color: var(--danger); font-size: 15px; }
</style>
