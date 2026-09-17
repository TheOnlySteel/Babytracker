<script lang="ts">
  import { Plus, ChevronDown, ChevronUp, RotateCcw } from '@lucide/svelte';
  import type { Entry } from '$lib/data/types';
  import { store } from '$lib/data/store.svelte';
  import { openSheet, type SheetKind } from '$lib/data/ui.svelte';
  import { entryMagnitude, headline, iconFor, sameAgainLabel, runningElapsed } from '$lib/data/derive';
  import { fmtAgo, fmtDuration } from '$lib/data/format';
  import type { BreastfeedPayload } from '$lib/data/types';
  import { dayKey, ofCard } from '$lib/data/summary';
  import Icon from '$lib/ui/Icon.svelte';
  import LogRow from './LogRow.svelte';

  let {
    card,
    title,
    color,
    lastLabel,
    sheetFor,
    sameAgain = false
  }: {
    card: 'feed' | 'diaper' | 'pump';
    title: string;
    color: string;
    lastLabel: string;
    /** which sheet the + opens, or a function of the last entry for editing */
    sheetFor: SheetKind;
    sameAgain?: boolean;
  } = $props();

  let expanded = $state(false);
  let busy = $state(false);

  const all = $derived(ofCard(store.entries, card));
  const last = $derived(all.find((e) => e.ended_at !== null || (e.type !== 'breastfeed' && e.type !== 'pump')));
  const running = $derived(all.find((e) => e.ended_at === null && (e.type === 'breastfeed' || e.type === 'pump')));
  const todayKey = $derived(dayKey(store.now));
  const ydKey = $derived(dayKey(new Date(store.now.getTime() - 86400_000)));
  const recent = $derived(all.filter((e) => {
    const k = dayKey(new Date(e.started_at));
    return k === todayKey || k === ydKey;
  }));
  // Bars are scaled per unit: bottles against the biggest bottle, breastfeeds against the longest feed.
  const max = $derived.by(() => {
    const m = { ml: 0, s: 0 };
    for (const e of recent) {
      const g = entryMagnitude(e);
      if (g && g.value > m[g.unit]) m[g.unit] = g.value;
    }
    return m;
  });
  const head = $derived(last ? headline(last) : null);

  function editSheet(e: Entry): SheetKind {
    return e.type === 'breastfeed' || e.type === 'combo' ? 'breastfeed' : e.type === 'bottle' ? 'bottle' : e.type === 'diaper' ? 'diaper' : 'pump';
  }

  async function repeat() {
    if (!last || busy) return;
    busy = true;
    try {
      const now = new Date();
      let ended: Date | null = null;
      if (last.type === 'breastfeed' || last.type === 'combo') {
        const p = last.payload as { left_s: number; right_s: number };
        ended = new Date(now.getTime() + (p.left_s + p.right_s) * 1000);
      }
      const payload = { ...(last.payload as Record<string, unknown>) };
      if ('segments' in payload) payload.segments = [];
      if ('manual' in payload) payload.manual = true;
      await store.insert({ type: last.type, started_at: now, ended_at: ended, payload: payload as never }, { undoLabel: `Logged ${sameAgainLabel(last).toLowerCase()}` });
    } finally {
      busy = false;
    }
  }
</script>

<section class="card">
  <div class="band" style:background={color}>
    <h2>{title}</h2>
    <button class="plus" onclick={() => openSheet(running ? editSheet(running) : sheetFor, running)} aria-label="Add {title}">
      <Plus size={30} strokeWidth={2.4} />
    </button>
  </div>

  {#if running}
    <button class="lastrow running" onclick={() => openSheet(editSheet(running), running)}>
      <Icon kind={iconFor(running)} />
      <div class="mid">
        <div class="lbl">Timer running</div>
        <div class="muted">started {fmtAgo(running.started_at, store.now)}</div>
      </div>
      <div class="big live">{running.type === 'breastfeed' ? fmtDuration(runningElapsed(running.payload as BreastfeedPayload, store.now).left_s + runningElapsed(running.payload as BreastfeedPayload, store.now).right_s) : fmtDuration((store.now.getTime() - new Date(running.started_at).getTime()) / 1000)}</div>
    </button>
  {:else if last && head}
    <button class="lastrow" onclick={() => openSheet(editSheet(last), last)}>
      <Icon kind={iconFor(last)} />
      <div class="mid">
        <div class="lbl">{lastLabel}</div>
        <div class="muted">{fmtAgo(last.started_at, store.now)}</div>
      </div>
      <div class="big">{head.big}{#if head.unit}<sup>{head.unit}</sup>{/if}</div>
    </button>
  {:else}
    <div class="lastrow empty muted">Nothing logged yet</div>
  {/if}

  {#if sameAgain && last && !running}
    <button class="again" onclick={repeat} disabled={busy}>
      <RotateCcw size={18} />
      <span>Same again</span>
      <span class="muted">{sameAgainLabel(last)}</span>
    </button>
  {/if}

  <button class="more" onclick={() => (expanded = !expanded)} aria-expanded={expanded}>
    <span>{expanded ? 'Show Less' : 'Show More'}</span>
    {#if expanded}<ChevronUp size={22} />{:else}<ChevronDown size={22} />{/if}
  </button>

  {#if expanded}
    <div class="log">
      {#each recent as e (e.id)}
        <LogRow entry={e} {max} onclick={() => openSheet(editSheet(e), e)} />
      {:else}
        <div class="muted none">Nothing today or yesterday</div>
      {/each}
    </div>
  {/if}
</section>

<style>
  .card { background: var(--card); border-radius: var(--radius); margin: 0 16px 20px; overflow: hidden; }
  .band { position: relative; height: 56px; display: flex; align-items: center; padding: 0 20px; }
  .band h2 { color: var(--ink); font-size: 30px; }
  .plus {
    position: absolute; right: 20px; bottom: -28px; width: 56px; height: 56px; border-radius: 50%;
    background: var(--accent); color: #fff; display: grid; place-items: center;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.35); z-index: 1;
  }
  .plus:active { transform: scale(0.96); }
  .lastrow {
    display: flex; align-items: center; gap: 20px; width: 100%; text-align: left;
    padding: 36px 20px 24px; min-height: 120px;
  }
  .lastrow.empty { padding-top: 40px; }
  .mid { flex: 1; min-width: 0; }
  .lbl { font-family: var(--serif); font-size: 24px; }
  .big { font-family: var(--serif); font-size: 52px; line-height: 1; white-space: nowrap; font-variant-numeric: tabular-nums; }
  .big sup { font-size: 18px; vertical-align: top; margin-left: 2px; position: relative; top: 2px; }
  .running .big { color: var(--feed); }
  .again {
    display: flex; align-items: center; gap: 10px; width: 100%; min-height: 52px; padding: 0 20px;
    border-top: 1px solid var(--rule); font-weight: 600; color: #8fb0e0;
  }
  .again:disabled { opacity: 0.5; }
  .again .muted { font-weight: 400; margin-left: auto; }
  .more {
    display: flex; align-items: center; justify-content: space-between; width: 100%;
    min-height: 56px; padding: 0 20px; border-top: 1px solid var(--rule); font-size: 17px;
  }
  .none { padding: 16px 20px; border-top: 1px solid var(--rule); }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }
</style>
