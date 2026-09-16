<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import type { BreastfeedPayload } from '$lib/data/types';
  import { store } from '$lib/data/store.svelte';
  import { openSheet, ui } from '$lib/data/ui.svelte';
  import { runningElapsed } from '$lib/data/derive';

  const running = $derived(store.entries.find((e) => e.type === 'breastfeed' && e.ended_at === null && !e.deleted_at));
  let tick = $state(new Date());
  let t: ReturnType<typeof setInterval>;
  onMount(() => { t = setInterval(() => (tick = new Date()), 1000); });
  onDestroy(() => clearInterval(t));
  const el = $derived(running ? runningElapsed(running.payload as BreastfeedPayload, tick) : null);
  const clock = (s: number) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
</script>

{#if running && el && ui.sheet?.kind !== 'breastfeed'}
  <button class="banner" onclick={() => openSheet('breastfeed', running)}>
    <span class="dot" class:paused={!el.open}></span>
    <span class="txt">Breastfeeding {el.open ? `· ${el.open}` : '· paused'}</span>
    <span class="clock">{clock(el.left_s + el.right_s)}</span>
    <span class="stop">Open</span>
  </button>
{/if}

<style>
  .banner {
    position: sticky; top: 0; z-index: 20; width: 100%;
    display: flex; align-items: center; gap: 12px; padding: calc(var(--safe-t) + 10px) 20px 10px;
    background: var(--feed); color: var(--ink); font-weight: 600;
  }
  .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--danger); animation: pulse 1.2s infinite; }
  .dot.paused { background: var(--ink); animation: none; opacity: 0.5; }
  .txt { flex: 1; text-align: left; text-transform: capitalize; }
  .clock { font-family: var(--serif); font-size: 22px; font-variant-numeric: tabular-nums; }
  .stop { border: 1.5px solid var(--ink); border-radius: 16px; padding: 4px 12px; font-size: 14px; }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
</style>
