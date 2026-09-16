<script lang="ts">
  import { onMount, onDestroy, untrack } from 'svelte';
  import type { BreastfeedPayload, Entry, Side } from '$lib/data/types';
  import { store } from '$lib/data/store.svelte';
  import { closeSheet } from '$lib/data/ui.svelte';
  import { lastOf, runningElapsed } from '$lib/data/derive';
  import { fmtDuration } from '$lib/data/format';
  import Sheet from '$lib/ui/Sheet.svelte';
  import TimeRow from '$lib/ui/TimeRow.svelte';
  import NoteRow from '$lib/ui/NoteRow.svelte';

  let { entry }: { entry?: Entry } = $props();

  // Three modes: editing a finished entry, driving the household's running timer, or a fresh sheet.
  const editing = untrack(() => (entry && entry.ended_at !== null && (entry.type === 'breastfeed' || entry.type === 'combo') ? entry : undefined));
  const running = $derived(store.entries.find((e) => e.type === 'breastfeed' && e.ended_at === null && !e.deleted_at));
  const p0 = editing?.payload as BreastfeedPayload | undefined;
  const lastFeed = lastOf(store.entries, (e) => e.type === 'breastfeed' && e.ended_at !== null);
  const suggested: Side = (lastFeed?.payload as BreastfeedPayload | undefined)?.end_side === 'left' ? 'right' : 'left';

  let startedAt = $state(editing ? new Date(editing.started_at) : new Date());
  let leftMin = $state<number | null>(p0 ? Math.round(p0.left_s / 60) : null);
  let rightMin = $state<number | null>(p0 ? Math.round(p0.right_s / 60) : null);
  let manual = $state(p0?.manual ?? false);
  let note = $state(editing?.note ?? '');
  let saving = $state(false);
  let tick = $state(new Date());
  let timer: ReturnType<typeof setInterval>;
  let wake: { release(): Promise<void> } | null = null;

  const elapsed = $derived(running ? runningElapsed(running.payload as BreastfeedPayload, tick) : null);

  onMount(async () => {
    timer = setInterval(() => (tick = new Date()), 1000);
    try {
      const nav = navigator as Navigator & { wakeLock?: { request(t: 'screen'): Promise<{ release(): Promise<void> }> } };
      wake = (await nav.wakeLock?.request('screen')) ?? null;
    } catch {
      /* unsupported or denied */
    }
  });
  onDestroy(() => {
    clearInterval(timer);
    wake?.release().catch(() => {});
  });

  async function tap(side: Side) {
    const now = new Date();
    if (!running) {
      await store.insert({
        type: 'breastfeed',
        started_at: now,
        ended_at: null,
        payload: { begin_side: side, end_side: null, left_s: 0, right_s: 0, manual: false, segments: [{ side, start: now.toISOString(), end: null }] }
      });
      manual = false;
      return;
    }
    const p = running.payload as BreastfeedPayload;
    const segs = p.segments.map((s) => ({ ...s }));
    const open = segs.find((s) => !s.end);
    if (open) open.end = now.toISOString();
    if (!open || open.side !== side) segs.push({ side, start: now.toISOString(), end: null });
    const e = runningElapsed({ ...p, segments: segs }, now);
    await store.update(running.id, { payload: { ...p, segments: segs, left_s: e.left_s, right_s: e.right_s } });
  }

  async function save() {
    saving = true;
    try {
      if (running && !manual) {
        const now = new Date();
        const p = running.payload as BreastfeedPayload;
        const segs = p.segments.map((s) => (s.end ? s : { ...s, end: now.toISOString() }));
        const e = runningElapsed({ ...p, segments: segs }, now);
        const last = segs[segs.length - 1];
        await store.update(
          running.id,
          {
            ended_at: now.toISOString(),
            payload: { ...p, segments: segs, left_s: e.left_s, right_s: e.right_s, end_side: last?.side ?? p.begin_side, manual: false },
            note: note || null
          },
          { undoLabel: `Logged ${fmtDuration(e.left_s + e.right_s)} breastfeed` }
        );
        closeSheet();
        return;
      }
      const left_s = Math.round((leftMin ?? 0) * 60);
      const right_s = Math.round((rightMin ?? 0) * 60);
      if (left_s + right_s <= 0) {
        alert('Enter a duration');
        return;
      }
      const begin_side: Side = left_s > 0 && right_s > 0 ? (p0?.begin_side ?? suggested) : left_s > 0 ? 'left' : 'right';
      const end_side: Side = left_s > 0 && right_s > 0 ? (p0?.end_side ?? (begin_side === 'left' ? 'right' : 'left')) : begin_side;
      const payload: BreastfeedPayload = { begin_side, end_side, left_s, right_s, manual: true, segments: [] };
      const ended = new Date(startedAt.getTime() + (left_s + right_s) * 1000);
      if (running && manual) {
        await store.update(running.id, { started_at: startedAt.toISOString(), ended_at: ended.toISOString(), payload, note: note || null }, { undoLabel: 'Logged breastfeed' });
      } else if (editing) {
        await store.update(editing.id, { started_at: startedAt.toISOString(), ended_at: ended.toISOString(), payload: { ...(editing.payload as object), ...payload }, note: note || null }, { undoLabel: 'Updated breastfeed' });
      } else {
        await store.insert({ type: 'breastfeed', started_at: startedAt, ended_at: ended, payload, note: note || null }, { undoLabel: `Logged ${fmtDuration(left_s + right_s)} breastfeed` });
      }
      closeSheet();
    } finally {
      saving = false;
    }
  }

  async function discard() {
    if (running) await store.remove(running.id, 'Timer discarded');
    else if (editing) await store.remove(editing.id, 'Breastfeed deleted');
    closeSheet();
  }

  const numVal = (e: Event) => {
    const v = (e.target as HTMLInputElement).value;
    manual = true;
    return v === '' ? null : Number(v);
  };
  const clock = (s: number) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
</script>

<Sheet title="Breastfeed" color="var(--feed)" dark onsave={save} {saving}>
  {#if !editing}
    <div class="sides">
      <button class="side" class:on={elapsed?.open === 'left'} class:hint={!running && suggested === 'left'} onclick={() => tap('left')}>
        <span class="name">Left</span>
        <span class="clock">{elapsed ? clock(elapsed.left_s) : '0:00'}</span>
        {#if elapsed?.open === 'left'}<span class="state">tap to pause</span>{:else if !running && suggested === 'left'}<span class="state">next side</span>{:else}<span class="state">&nbsp;</span>{/if}
      </button>
      <button class="side" class:on={elapsed?.open === 'right'} class:hint={!running && suggested === 'right'} onclick={() => tap('right')}>
        <span class="name">Right</span>
        <span class="clock">{elapsed ? clock(elapsed.right_s) : '0:00'}</span>
        {#if elapsed?.open === 'right'}<span class="state">tap to pause</span>{:else if !running && suggested === 'right'}<span class="state">next side</span>{:else}<span class="state">&nbsp;</span>{/if}
      </button>
    </div>
    {#if elapsed}
      <div class="total serif">{clock(elapsed.left_s + elapsed.right_s)}&nbsp;<span class="muted">total{#if !elapsed.open} · paused{/if}</span></div>
    {:else}
      <p class="muted or">or type the durations below</p>
    {/if}
  {/if}

  {#if !running || manual}
    <TimeRow bind:value={startedAt} />
  {/if}
  <label class="row">
    <span class="row-label">Left</span>
    <span class="amt"><input type="number" inputmode="numeric" placeholder="0" value={leftMin ?? ''} oninput={(e) => (leftMin = numVal(e))} /> min</span>
  </label>
  <label class="row">
    <span class="row-label">Right</span>
    <span class="amt"><input type="number" inputmode="numeric" placeholder="0" value={rightMin ?? ''} oninput={(e) => (rightMin = numVal(e))} /> min</span>
  </label>
  {#if running && manual}
    <p class="muted hint-text">Typed durations replace the timer when you save.</p>
  {/if}
  <NoteRow bind:value={note} />

  {#snippet footer()}
    {#if running || editing}
      <button class="btn-ghost danger" onclick={discard}>{running ? 'Discard timer' : 'Delete'}</button>
    {/if}
  {/snippet}
</Sheet>

<style>
  .sides { display: flex; gap: 16px; padding: 24px 20px 8px; }
  .side {
    flex: 1; min-height: 120px; border-radius: 20px; border: 1.5px solid var(--accent); color: var(--text);
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
  }
  .side.hint { background: var(--accent-soft); }
  .side.on { background: var(--accent); color: #fff; }
  .name { font-family: var(--serif); font-size: 26px; }
  .clock { font-family: var(--serif); font-size: 40px; font-variant-numeric: tabular-nums; line-height: 1; }
  .state { font-size: 13px; opacity: 0.8; }
  .total { text-align: center; font-size: 22px; padding: 8px 0 16px; }
  .or { text-align: center; margin: 0; padding: 4px 0 12px; font-size: 14px; }
  .amt { display: inline-flex; align-items: baseline; gap: 6px; }
  .amt input { width: 80px; text-align: right; background: var(--card-2); border: 0; border-radius: 8px; padding: 8px 10px; font-size: 20px; outline: none; }
  .hint-text { margin: 0; padding: 8px 20px; font-size: 14px; }
  .danger { color: var(--danger); border-color: var(--danger); width: 100%; }
</style>
