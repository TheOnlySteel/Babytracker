<script lang="ts">
  import type { Entry } from '$lib/data/types';
  import { entryLabel } from '$lib/data/derive';
  import { fmtWhen } from '$lib/data/format';
  import { store } from '$lib/data/store.svelte';

  /**
   * Persistent conflict state for a sheet whose save was refused because the other phone
   * changed the entry first. Neither choice overwrites silently: "Use theirs" discards this
   * draft and reopens with the latest; "Keep mine" re-applies the draft on top of the latest
   * version, which is an explicit decision by the caregiver who has now seen both.
   */
  let { latest, onUseTheirs, onKeepMine }: { latest: Entry; onUseTheirs: () => void; onKeepMine: () => void } = $props();
  const who = $derived(store.caregivers.find((c) => c.user_id === latest.updated_by)?.display_name ?? 'the other phone');
</script>

<div class="conflict" role="alert">
  <p class="t">Changed by {who} while you were editing</p>
  <p class="d">Their version: {fmtWhen(latest.started_at)} · {entryLabel(latest)}{#if latest.note} · “{latest.note}”{/if}</p>
  <div class="actions">
    <button class="btn-ghost" onclick={onUseTheirs}>Use theirs</button>
    <button class="btn-primary" onclick={onKeepMine}>Keep mine</button>
  </div>
</div>

<style>
  .conflict { margin: 12px 20px; padding: 14px 16px; border-radius: 12px; background: rgba(229, 72, 77, 0.14); border: 1.5px solid var(--danger); }
  .t { margin: 0 0 4px; font-weight: 600; }
  .d { margin: 0 0 12px; font-size: 15px; color: var(--muted); }
  .actions { display: flex; gap: 10px; }
  .actions button { flex: 1; }
</style>
