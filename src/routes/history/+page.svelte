<script lang="ts">
  import { ChevronDown, ChevronUp } from '@lucide/svelte';
  import Header from '$lib/components/Header.svelte';
  import LogRow from '$lib/components/LogRow.svelte';
  import { store } from '$lib/data/store.svelte';
  import { groupByDay, summarize, feedSessions } from '$lib/data/summary';
  import { fmtDayLabel, fmtDuration } from '$lib/data/format';
  import { openSheet, type SheetKind } from '$lib/data/ui.svelte';
  import type { Entry } from '$lib/data/types';

  const days = $derived(groupByDay(store.entries.filter((e) => e.type !== 'growth')));
  let open = $state<Set<string>>(new Set());
  let loading = $state(false);
  let initialised = false;

  function toggle(day: string) {
    const next = new Set(open);
    if (next.has(day)) next.delete(day); else next.add(day);
    open = next;
  }
  function editSheet(e: Entry): SheetKind {
    return e.type === 'breastfeed' || e.type === 'combo' ? 'breastfeed' : e.type === 'bottle' ? 'bottle' : e.type === 'diaper' ? 'diaper' : 'pump';
  }
  function dayStats(day: string, entries: Entry[]) {
    const [y, m, d] = day.split('-').map(Number);
    const from = new Date(y, m - 1, d);
    const to = new Date(y, m - 1, d + 1);
    return summarize(entries, from, to, store.child?.birth_date, store.now);
  }
  async function older() {
    loading = true;
    // Pages by date window from the store's cursor, so an empty month is skipped rather than
    // mistaken for the end of history; the store marks exhausted once it passes the birth date.
    await store.loadOlder(30);
    loading = false;
  }
  $effect(() => {
    // Open the newest day once on first render; after that the user's collapses stick.
    if (!initialised && days.length) {
      initialised = true;
      open = new Set([days[0].day]);
    }
  });
</script>

<Header />
<main>
  <h2>History</h2>
  {#each days as { day, entries } (day)}
    {@const s = dayStats(day, entries)}
    {@const isOpen = open.has(day)}
    <section class="day">
      <button class="dayhead" onclick={() => toggle(day)} aria-expanded={isOpen}>
        <div>
          <div class="serif lbl">{fmtDayLabel(day, store.now)}</div>
          <div class="muted stats">
            {feedSessions(entries)} feeds · {fmtDuration(s.breastfeed.total_s)} · {s.bottle.total_ml} mL · {s.diaper.count} diapers{#if s.pump.count}&nbsp;· {s.pump.total_ml} mL pumped{/if}
          </div>
        </div>
        {#if isOpen}<ChevronUp size={22} />{:else}<ChevronDown size={22} />{/if}
      </button>
      {#if isOpen}
        {#each entries as e (e.id)}
          <LogRow entry={e} showDay={false} onclick={() => openSheet(editSheet(e), e)} />
        {/each}
      {/if}
    </section>
  {:else}
    <p class="muted empty">Nothing logged yet.</p>
  {/each}
  {#if !store.exhausted}
    <button class="btn-ghost more" onclick={older} disabled={loading}>{loading ? 'Loading…' : `Load older (before ${new Date(store.loadedSince).toLocaleDateString('en-US', { month: 'short', day: 'numeric' })})`}</button>
  {:else}
    <p class="muted more">That's everything since birth.</p>
  {/if}
</main>

<style>
  main { padding: 0 16px calc(var(--tab-h) + var(--safe-b) + 24px); }
  h2 { font-size: 30px; padding: 8px 4px 12px; }
  .day { background: var(--card); border-radius: var(--radius); margin-bottom: 12px; overflow: hidden; }
  .dayhead { display: flex; align-items: center; justify-content: space-between; width: 100%; text-align: left; padding: 14px 20px; min-height: 64px; }
  .lbl { font-size: 24px; }
  .stats { font-size: 14px; }
  .empty { padding: 20px; }
  .more { display: block; margin: 8px auto; }
</style>
