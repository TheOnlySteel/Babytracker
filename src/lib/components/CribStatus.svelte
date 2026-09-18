<script lang="ts">
  import { store } from '$lib/data/store.svelte';
  const status = $derived(store.sleepStatus);
  const age = $derived(
    status?.observed_at
      ? Math.max(
          0,
          Math.floor((+store.now - Date.parse(status.observed_at)) / 60000),
        )
      : null,
  );
  const stale = $derived(age === null || age > 3);
  const bad = $derived(age === null || age > 15 || !!status?.source_error);
</script>

<div class="crib" class:stale class:bad role="status">
  <span aria-hidden="true">{bad ? '○' : '●'}</span>
  <span
    >Crib: {status?.status ?? 'not connected'}{#if status?.since}
      · since {new Date(status.since).toLocaleTimeString([], {
        hour: 'numeric',
        minute: '2-digit',
      })}{/if}
    {#if age !== null && stale}
      · crib update {age} min old{/if}{#if status?.source_error}
      · {status.source_error.replaceAll('_', ' ')}{/if}
  </span>
</div>

<style>
  .crib {
    display: flex;
    gap: 8px;
    font-size: 14px;
    padding: 20px 20px 0;
    color: var(--muted);
  }
  .stale {
    color: #ecc48d;
  }
  .bad {
    color: #f2a5a5;
  }
</style>
