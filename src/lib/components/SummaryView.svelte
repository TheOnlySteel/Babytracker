<script lang="ts">
  import { AlertTriangle } from '@lucide/svelte';
  import { store } from '$lib/data/store.svelte';
  import { summarize, windowFor } from '$lib/data/summary';
  import { fmtDuration } from '$lib/data/format';
  import Icon from '$lib/ui/Icon.svelte';

  let mode = $state<'today' | '24h'>('today');
  const s = $derived.by(() => {
    const { from, to } = windowFor(mode, store.now);
    return summarize(store.entries, from, to, store.child?.birth_date);
  });
</script>

<div class="seg" role="tablist">
  <button role="tab" aria-selected={mode === 'today'} onclick={() => (mode = 'today')}>Today</button>
  <button role="tab" aria-selected={mode === '24h'} onclick={() => (mode = '24h')}>Last 24 Hours</button>
</div>

<div class="srow">
  <Icon kind="breast" size={56} />
  <div>
    <div class="t"><span class="serif">Breastfeed</span><span class="badge" style:background="var(--breastfeed-badge)">{s.breastfeed.count}</span></div>
    <div>{fmtDuration(s.breastfeed.total_s)} total</div>
    <div>{fmtDuration(s.breastfeed.left_s)} left</div>
    <div>{fmtDuration(s.breastfeed.right_s)} right</div>
  </div>
</div>
<div class="srow">
  <Icon kind="bottle" size={56} />
  <div>
    <div class="t"><span class="serif">Bottle Feed</span><span class="badge" style:background="var(--bottle-badge)">{s.bottle.count}</span></div>
    <div>{s.bottle.total_ml} mL total</div>
    <div>{s.bottle.breast_milk_ml} mL breast milk</div>
    <div>{s.bottle.formula_ml} mL formula</div>
  </div>
</div>
<div class="srow">
  <Icon kind="diaper" size={56} />
  <div>
    <div class="t"><span class="serif">Diaper</span><span class="badge" style:background="var(--diaper)">{s.diaper.count}</span>
      {#if s.diaper.flagged}<span class="flag" title="Unusual stool colour logged"><AlertTriangle size={18} /> colour</span>{/if}
    </div>
    <div>{s.diaper.wet} wet</div>
    <div>{s.diaper.dirty} dirty</div>
    {#if s.diaper.dry}<div>{s.diaper.dry} dry</div>{/if}
  </div>
</div>
<div class="srow">
  <Icon kind="pump" size={56} />
  <div>
    <div class="t"><span class="serif">Pump</span><span class="badge" style:background="var(--pump)">{s.pump.count}</span></div>
    <div>{s.pump.total_ml} mL total</div>
    <div>{s.pump.left_ml} mL left</div>
    <div>{s.pump.right_ml} mL right</div>
  </div>
</div>

<style>
  .seg {
    display: grid; grid-template-columns: 1fr 1fr; margin: 16px 20px; padding: 3px; border-radius: 22px; background: #3a4154;
  }
  .seg button { min-height: 44px; border-radius: 22px; font-weight: 500; color: var(--text); }
  .seg button[aria-selected='true'] { background: #5b6478; font-weight: 600; }
  .srow { display: flex; gap: 20px; padding: 20px; border-top: 1px solid var(--rule); font-size: 19px; line-height: 1.5; }
  .t { display: flex; align-items: center; gap: 10px; margin-bottom: 4px; }
  .serif { font-size: 26px; }
  .badge { color: var(--ink); font-size: 16px; font-weight: 600; padding: 2px 12px; border-radius: 12px; font-family: var(--sans); }
  .flag { display: inline-flex; align-items: center; gap: 4px; color: var(--danger); font-size: 14px; font-weight: 600; }
</style>
