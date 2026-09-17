<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import type { BreastfeedPayload } from '$lib/data/types';
  import { store } from '$lib/data/store.svelte';
  import { openSheet, ui } from '$lib/data/ui.svelte';
  import { runningElapsed } from '$lib/data/derive';
  import { fmtDuration } from '$lib/data/format';

  const running = $derived(store.entries.find((e) => e.type === 'breastfeed' && e.ended_at === null && !e.deleted_at));
  let tick = $state(new Date());
  let stopping = $state(false);
  let t: ReturnType<typeof setInterval>;
  onMount(() => {
    t = setInterval(() => (tick = new Date()), 1000);
  });
  onDestroy(() => clearInterval(t));
  const el = $derived(running ? runningElapsed(running.payload as BreastfeedPayload, tick) : null);
  const clock = (s: number) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;

  /** Spec §6: the banner can stop the timer directly, from any screen, without opening the sheet. */
  async function stop(e: MouseEvent) {
    e.stopPropagation();
    const cur = running;
    if (!cur || stopping) return;
    stopping = true;
    try {
      const now = new Date();
      const p = cur.payload as BreastfeedPayload;
      const segs = p.segments.map((s) => (s.end ? s : { ...s, end: now.toISOString() }));
      const done = runningElapsed({ ...p, segments: segs }, now);
      const last = segs[segs.length - 1];
      await store.update(
        cur.id,
        { ended_at: now.toISOString(), payload: { ...p, segments: segs, left_s: done.left_s, right_s: done.right_s, end_side: last?.side ?? p.begin_side, manual: false } },
        { undoLabel: `Logged ${fmtDuration(done.left_s + done.right_s)} breastfeed`, expectedUpdatedAt: cur.updated_at }
      );
    } catch {
      /* toast shown */
    } finally {
      stopping = false;
    }
  }
</script>

{#if running && el && ui.sheet?.kind !== 'breastfeed'}
  <div class="banner">
    <button class="open" onclick={() => openSheet('breastfeed', running)}>
      <span class="dot" class:paused={!el.open}></span>
      <span class="txt">Breastfeeding {el.open ? `· ${el.open}` : '· paused'}</span>
      <span class="clock">{clock(el.left_s + el.right_s)}</span>
    </button>
    <button class="stop" onclick={stop} disabled={stopping}>Stop</button>
  </div>
{/if}

<style>
  .banner {
    position: sticky; top: 0; z-index: 20; width: 100%;
    display: flex; align-items: center; gap: 8px; padding: calc(var(--safe-t) + 6px) 12px 6px 20px;
    background: var(--feed); color: var(--ink); font-weight: 600;
  }
  .open { flex: 1; display: flex; align-items: center; gap: 12px; min-height: 44px; text-align: left; }
  .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--danger); animation: pulse 1.2s infinite; }
  .dot.paused { background: var(--ink); animation: none; opacity: 0.5; }
  .txt { flex: 1; text-transform: capitalize; }
  .clock { font-family: var(--serif); font-size: 22px; font-variant-numeric: tabular-nums; }
  .stop { min-height: 44px; min-width: 64px; border: 1.5px solid var(--ink); border-radius: 22px; padding: 0 14px; font-size: 15px; }
  .stop:disabled { opacity: 0.5; }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
</style>
