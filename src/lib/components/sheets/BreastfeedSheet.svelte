<script lang="ts">
  import { onMount, onDestroy, untrack } from 'svelte';
  import type { BreastfeedPayload, Entry, Side } from '$lib/data/types';
  import { store, ConflictError } from '$lib/data/store.svelte';
  import { closeSheet, openSheet } from '$lib/data/ui.svelte';
  import { lastOf, runningElapsed } from '$lib/data/derive';
  import { fmtDuration } from '$lib/data/format';
  import { nonNegative } from '$lib/data/validate';
  import Sheet from '$lib/ui/Sheet.svelte';
  import ConflictBar from '$lib/ui/ConflictBar.svelte';
  import TimeRow from '$lib/ui/TimeRow.svelte';
  import NoteRow from '$lib/ui/NoteRow.svelte';

  let { entry }: { entry?: Entry } = $props();

  // Two mutually exclusive modes, fixed when the sheet opens:
  //  - 'edit':  a finished entry was opened. Only that entry is ever written. The household's
  //             running timer, if any, is irrelevant here.
  //  - 'timer': fresh sheet, or the running entry itself. Drives the household's one running timer.
  const opened = untrack(() => entry);
  const mode: 'edit' | 'timer' = opened && opened.ended_at !== null ? 'edit' : 'timer';
  const editing = mode === 'edit' ? opened! : undefined;
  const running = $derived(mode === 'timer' ? store.entries.find((e) => e.type === 'breastfeed' && e.ended_at === null && !e.deleted_at) : undefined);

  const p0 = editing?.payload as BreastfeedPayload | undefined;
  const lastFeed = lastOf(store.entries, (e) => e.type === 'breastfeed' && e.ended_at !== null);
  const suggested: Side = (lastFeed?.payload as BreastfeedPayload | undefined)?.end_side === 'left' ? 'right' : 'left';

  const startedAt0 = editing ? new Date(editing.started_at) : new Date();
  let startedAt = $state(new Date(startedAt0));
  let leftMin = $state<number | null>(p0 ? Math.round(p0.left_s / 60) : null);
  let rightMin = $state<number | null>(p0 ? Math.round(p0.right_s / 60) : null);
  /** Set when the user types a duration. Until then, exact stored timings are preserved on save. */
  let durationsTouched = $state(false);
  let note = $state(editing?.note ?? '');
  let saving = $state(false);
  let err = $state('');
  /** Version token the next save is conditional on. Advanced only by an explicit "Keep mine". */
  let version = $state(editing?.updated_at);
  let conflict = $state<Entry | null>(null);
  let tick = $state(new Date());
  let timer: ReturnType<typeof setInterval>;
  let wake: { release(): Promise<void> } | null = null;

  /** Timer transitions run one at a time; while one is in flight the buttons show it and ignore taps. */
  let pendingSide = $state<Side | null>(null);
  let queue: Promise<unknown> = Promise.resolve();

  const elapsed = $derived(running ? runningElapsed(running.payload as BreastfeedPayload, tick) : null);
  const snapshot = () => JSON.stringify([startedAt.getTime(), leftMin, rightMin, note]);
  const initial = snapshot();
  // A fresh timer sheet with nothing typed is never "dirty": a running timer lives in the database, not the form.
  const dirty = $derived(snapshot() !== initial);

  async function acquireWake() {
    try {
      const nav = navigator as Navigator & { wakeLock?: { request(t: 'screen'): Promise<{ release(): Promise<void> }> } };
      wake = (await nav.wakeLock?.request('screen')) ?? null;
    } catch {
      /* unsupported or denied */
    }
  }
  function onVisibility() {
    // iOS releases the wake lock when the app is backgrounded; take it again on return.
    if (document.visibilityState === 'visible' && mode === 'timer') acquireWake();
  }
  onMount(() => {
    timer = setInterval(() => (tick = new Date()), 1000);
    if (mode === 'timer') acquireWake();
    document.addEventListener('visibilitychange', onVisibility);
  });
  onDestroy(() => {
    clearInterval(timer);
    document.removeEventListener('visibilitychange', onVisibility);
    wake?.release().catch(() => {});
  });

  function tap(side: Side) {
    if (pendingSide || saving) return; // one intended operation per tap; no queued start+pause surprises
    pendingSide = side;
    queue = queue
      .then(() => transition(side))
      .catch((e) => {
        if (e instanceof ConflictError) {
          // The other phone moved the timer; the store already refreshed it. Nothing to reapply for a tap.
          err = 'The timer changed on the other phone; showing the latest.';
        }
      })
      .finally(() => (pendingSide = null));
  }
  async function transition(side: Side) {
    const now = new Date();
    const cur = running;
    if (!cur) {
      await store.insert({
        type: 'breastfeed',
        started_at: now,
        ended_at: null,
        payload: { begin_side: side, end_side: null, left_s: 0, right_s: 0, manual: false, segments: [{ side, start: now.toISOString(), end: null }] }
      });
      return;
    }
    const p = cur.payload as BreastfeedPayload;
    const segs = p.segments.map((s) => ({ ...s }));
    const open = segs.find((s) => !s.end);
    if (open) open.end = now.toISOString();
    if (!open || open.side !== side) segs.push({ side, start: now.toISOString(), end: null });
    const e = runningElapsed({ ...p, segments: segs }, now);
    await store.update(cur.id, { payload: { ...p, segments: segs, left_s: e.left_s, right_s: e.right_s } }, { expectedUpdatedAt: cur.updated_at });
  }

  function typedDurations(): { left_s: number; right_s: number } | null {
    const l = nonNegative(leftMin, 'Left');
    const r = nonNegative(rightMin, 'Right');
    if (l.error || r.error) {
      err = l.error ?? r.error ?? '';
      return null;
    }
    const left_s = Math.round((l.value ?? 0) * 60);
    const right_s = Math.round((r.value ?? 0) * 60);
    if (left_s + right_s <= 0) {
      err = 'Enter a duration, or tap Left or Right to start the timer';
      return null;
    }
    return { left_s, right_s };
  }

  async function save() {
    if (saving) return;
    err = '';
    saving = true;
    try {
      // Let any in-flight Start/Pause/Switch finish first, then act on the timer as it now is.
      await queue;
      if (mode === 'edit') await saveEdit();
      else await saveTimer();
      closeSheet();
    } catch (e) {
      if (e instanceof ConflictError && e.latest) conflict = e.latest;
      /* other failures already produced a persistent toast; keep the form so nothing typed is lost */
    } finally {
      saving = false;
    }
  }

  async function saveEdit() {
    const e = editing!;
    const p = e.payload as BreastfeedPayload;
    if (durationsTouched) {
      const d = typedDurations();
      if (!d) throw new Error('invalid');
      const begin_side: Side = d.left_s > 0 && d.right_s > 0 ? (p.begin_side ?? suggested) : d.left_s > 0 ? 'left' : 'right';
      const end_side: Side = d.left_s > 0 && d.right_s > 0 ? (p.end_side ?? (begin_side === 'left' ? 'right' : 'left')) : begin_side;
      // Typing durations is the explicit "convert to manual" action: segments no longer describe the feed.
      const payload = { ...p, begin_side, end_side, left_s: d.left_s, right_s: d.right_s, manual: true, segments: [] };
      const ended = new Date(startedAt.getTime() + (d.left_s + d.right_s) * 1000);
      await store.update(e.id, { started_at: startedAt.toISOString(), ended_at: ended.toISOString(), payload, note: note || null }, { undoLabel: 'Updated breastfeed', expectedUpdatedAt: version });
      return;
    }
    // Note or start-time only: keep exact seconds, segments and the original wall-clock length.
    const patch: { started_at?: string; ended_at?: string | null; note: string | null } = { note: note || null };
    if (startedAt.getTime() !== startedAt0.getTime()) {
      const delta = startedAt.getTime() - startedAt0.getTime();
      patch.started_at = startedAt.toISOString();
      patch.ended_at = e.ended_at ? new Date(new Date(e.ended_at).getTime() + delta).toISOString() : null;
      if (p.segments?.length) {
        const shifted = p.segments.map((s) => ({ ...s, start: new Date(new Date(s.start).getTime() + delta).toISOString(), end: s.end ? new Date(new Date(s.end).getTime() + delta).toISOString() : null }));
        await store.update(e.id, { ...patch, payload: { ...p, segments: shifted } }, { undoLabel: 'Updated breastfeed', expectedUpdatedAt: version });
        return;
      }
    }
    await store.update(e.id, patch, { undoLabel: 'Updated breastfeed', expectedUpdatedAt: version });
  }

  async function saveTimer() {
    const cur = running;
    if (cur && !durationsTouched) {
      const now = new Date();
      const p = cur.payload as BreastfeedPayload;
      const segs = p.segments.map((s) => (s.end ? s : { ...s, end: now.toISOString() }));
      const el = runningElapsed({ ...p, segments: segs }, now);
      const last = segs[segs.length - 1];
      await store.update(
        cur.id,
        { ended_at: now.toISOString(), payload: { ...p, segments: segs, left_s: el.left_s, right_s: el.right_s, end_side: last?.side ?? p.begin_side, manual: false }, note: note || null },
        { undoLabel: `Logged ${fmtDuration(el.left_s + el.right_s)} breastfeed`, expectedUpdatedAt: cur.updated_at }
      );
      return;
    }
    const d = typedDurations();
    if (!d) throw new Error('invalid');
    const begin_side: Side = d.left_s > 0 && d.right_s > 0 ? suggested : d.left_s > 0 ? 'left' : 'right';
    const end_side: Side = d.left_s > 0 && d.right_s > 0 ? (begin_side === 'left' ? 'right' : 'left') : begin_side;
    const payload: BreastfeedPayload = { begin_side, end_side, left_s: d.left_s, right_s: d.right_s, manual: true, segments: [] };
    const ended = new Date(startedAt.getTime() + (d.left_s + d.right_s) * 1000);
    if (cur) {
      // Typed over a running timer: the typed values are the record.
      await store.update(cur.id, { started_at: startedAt.toISOString(), ended_at: ended.toISOString(), payload, note: note || null }, { undoLabel: 'Logged breastfeed', expectedUpdatedAt: cur.updated_at });
    } else {
      await store.insert({ type: 'breastfeed', started_at: startedAt, ended_at: ended, payload, note: note || null }, { undoLabel: `Logged ${fmtDuration(d.left_s + d.right_s)} breastfeed` });
    }
  }

  async function discard() {
    if (mode === 'edit') await store.remove(editing!.id, 'Breastfeed deleted', version);
    else if (running) await store.remove(running.id, 'Timer discarded', running.updated_at);
    closeSheet();
  }

  const numVal = (e: Event) => {
    const v = (e.target as HTMLInputElement).value;
    durationsTouched = true;
    err = '';
    return v === '' ? null : Number(v);
  };
  const clock = (s: number) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
  const busy = $derived(pendingSide !== null || saving);
</script>

<Sheet title="Breastfeed" color="var(--feed)" dark onsave={save} saving={busy} {dirty}>
  {#if mode === 'timer'}
    <div class="sides" aria-busy={pendingSide !== null}>
      <button class="side" class:on={elapsed?.open === 'left'} class:hint={!running && suggested === 'left'} class:pending={pendingSide === 'left'} onclick={() => tap('left')} disabled={busy}>
        <span class="name">Left</span>
        <span class="clock">{elapsed ? clock(elapsed.left_s) : '0:00'}</span>
        {#if pendingSide === 'left'}<span class="state">{running ? 'saving…' : 'starting…'}</span>
        {:else if elapsed?.open === 'left'}<span class="state">tap to pause</span>
        {:else if !running && suggested === 'left'}<span class="state">next side</span>
        {:else}<span class="state">&nbsp;</span>{/if}
      </button>
      <button class="side" class:on={elapsed?.open === 'right'} class:hint={!running && suggested === 'right'} class:pending={pendingSide === 'right'} onclick={() => tap('right')} disabled={busy}>
        <span class="name">Right</span>
        <span class="clock">{elapsed ? clock(elapsed.right_s) : '0:00'}</span>
        {#if pendingSide === 'right'}<span class="state">{running ? 'saving…' : 'starting…'}</span>
        {:else if elapsed?.open === 'right'}<span class="state">tap to pause</span>
        {:else if !running && suggested === 'right'}<span class="state">next side</span>
        {:else}<span class="state">&nbsp;</span>{/if}
      </button>
    </div>
    {#if elapsed}
      <div class="total serif">{clock(elapsed.left_s + elapsed.right_s)}&nbsp;<span class="muted">total{#if !elapsed.open} · paused{/if}</span></div>
    {:else}
      <p class="muted or">or type the durations below</p>
    {/if}
  {/if}

  {#if conflict}
    <ConflictBar
      latest={conflict}
      onUseTheirs={() => openSheet('breastfeed', conflict!)}
      onKeepMine={() => {
        version = conflict!.updated_at;
        conflict = null;
        save();
      }}
    />
  {/if}

  {#if mode === 'edit' || !running || durationsTouched}
    <TimeRow bind:value={startedAt} />
  {/if}
  <label class="row">
    <span class="row-label">Left</span>
    <span class="amt"><input type="number" inputmode="numeric" min="0" placeholder="0" value={leftMin ?? ''} oninput={(e) => (leftMin = numVal(e))} /> min</span>
  </label>
  <label class="row">
    <span class="row-label">Right</span>
    <span class="amt"><input type="number" inputmode="numeric" min="0" placeholder="0" value={rightMin ?? ''} oninput={(e) => (rightMin = numVal(e))} /> min</span>
  </label>
  {#if mode === 'edit' && !durationsTouched && p0 && !p0.manual}
    <p class="muted hint-text">Timed feed: exact seconds are kept unless you change the minutes.</p>
  {:else if mode === 'timer' && running && durationsTouched}
    <p class="muted hint-text">Typed durations replace the timer when you save.</p>
  {/if}
  {#if err}<p class="err" role="alert">{err}</p>{/if}
  <NoteRow bind:value={note} />

  {#snippet footer()}
    {#if mode === 'edit' || running}
      <button class="btn-link danger" onclick={discard} disabled={busy}>{mode === 'edit' ? 'Delete' : 'Discard timer'}</button>
    {/if}
  {/snippet}
</Sheet>

<style>
  .sides { display: flex; gap: 16px; padding: 24px 20px 8px; }
  .side {
    flex: 1; min-height: 120px; border-radius: 20px; border: 1.5px solid var(--accent-text); color: var(--text);
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
  }
  .side.hint { background: var(--accent-soft); }
  .side.on { background: var(--accent); color: #fff; }
  .side.pending { opacity: 0.85; border-style: dashed; }
  .side:disabled:not(.pending) { opacity: 0.6; }
  .name { font-family: var(--serif); font-size: 26px; }
  .clock { font-family: var(--serif); font-size: 40px; font-variant-numeric: tabular-nums; line-height: 1; }
  .state { font-size: 13px; opacity: 0.8; }
  .total { text-align: center; font-size: 22px; padding: 8px 0 16px; }
  .or { text-align: center; margin: 0; padding: 4px 0 12px; font-size: 14px; }
  .amt { display: inline-flex; align-items: baseline; gap: 6px; }
  .amt input { width: 80px; text-align: right; background: var(--card-2); border: 0; border-radius: 8px; padding: 8px 10px; font-size: 20px; outline: none; }
  .hint-text { margin: 0; padding: 8px 20px; font-size: 14px; }
  .err { margin: 0; padding: 8px 20px; color: var(--danger); font-size: 15px; }
</style>
