<script lang="ts">
  import { ChevronRight } from '@lucide/svelte';
  import type { Entry } from '$lib/data/types';
  import { entryLabel, entryMagnitude, iconFor } from '$lib/data/derive';
  import { fmtTime } from '$lib/data/format';
  import { dayKey } from '$lib/data/summary';
  import { store } from '$lib/data/store.svelte';
  import Icon from '$lib/ui/Icon.svelte';

  let { entry, max = { ml: 0, s: 0 }, onclick, showDay = true, color }: { entry: Entry; max?: { ml: number; s: number }; onclick?: () => void; showDay?: boolean; color?: string } = $props();

  const mag = $derived(entryMagnitude(entry));
  const pct = $derived(mag && max[mag.unit] > 0 ? Math.max(4, Math.round((mag.value / max[mag.unit]) * 100)) : 0);
  const isYesterday = $derived(showDay && dayKey(new Date(entry.started_at)) === dayKey(new Date(store.now.getTime() - 86400_000)));
  const running = $derived(entry.ended_at === null && (entry.type === 'breastfeed' || entry.type === 'pump' || entry.type === 'sleep'));
  const barColor = $derived(color ?? (entry.type === 'sleep' ? 'var(--sleep)' : entry.type === 'diaper' ? 'var(--diaper)' : entry.type === 'pump' ? 'var(--pump)' : 'var(--feed)'));
</script>

<button class="logrow" {onclick} disabled={!onclick}>
  <Icon kind={iconFor(entry)} size={36} />
  <div class="mid">
    <div class="line1">
      {#if isYesterday}<span class="yd">YD</span>{/if}
      <span class="time">{fmtTime(entry.started_at)}</span>
      <span class="label">{entryLabel(entry)}</span>
      {#if running}<span class="live">running</span>{/if}
    </div>
    {#if mag && !running}
      <div class="line2">
        <span class="bar" style:width="{pct}%" style:background={barColor}></span>
        <span class="val">{mag.text}</span>
      </div>
    {:else if entry.note}
      <div class="line2 muted note">{entry.note}</div>
    {/if}
  </div>
  <span class="by muted" title="logged by">{store.initial(entry.created_by)}</span>
  {#if onclick}<ChevronRight size={20} class="chev" />{/if}
</button>

<style>
  .logrow {
    display: flex; align-items: center; gap: 12px; width: 100%; text-align: left;
    padding: 12px 16px 12px 20px; min-height: 72px; border-top: 1px solid var(--rule);
  }
  .logrow:disabled { cursor: default; }
  .mid { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 6px; }
  .line1 { display: flex; align-items: baseline; gap: 10px; font-size: 19px; white-space: nowrap; overflow: hidden; }
  .time { font-variant-numeric: tabular-nums; }
  .label { overflow: hidden; text-overflow: ellipsis; }
  .yd { font-size: 14px; font-weight: 600; color: var(--muted); }
  .live { font-size: 13px; color: var(--pump); font-weight: 600; text-transform: uppercase; letter-spacing: 0.04em; }
  .line2 { display: flex; align-items: center; gap: 10px; }
  .bar { display: block; height: 7px; border-radius: 4px; max-width: calc(100% - 64px); transition: width 200ms; }
  .val { font-size: 16px; white-space: nowrap; }
  .note { font-size: 14px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .by { font-size: 12px; width: 14px; text-align: center; }
  :global(.chev) { color: var(--muted); flex: none; }
</style>
