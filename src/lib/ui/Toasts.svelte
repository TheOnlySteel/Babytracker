<script lang="ts">
  import { toasts, dismiss } from '$lib/data/toast.svelte';
</script>

<div class="toasts" aria-live="polite">
  {#each toasts.list as t (t.id)}
    <div class="toast">
      <span>{t.message}</span>
      {#if t.undo}
        <button
          onclick={async () => {
            dismiss(t.id);
            await t.undo?.();
          }}>Undo</button
        >
      {/if}
    </div>
  {/each}
</div>

<style>
  .toasts {
    position: fixed; left: 16px; right: 16px; bottom: calc(var(--tab-h) + var(--safe-b) + 12px);
    display: flex; flex-direction: column; gap: 8px; z-index: 60; pointer-events: none;
  }
  .toast {
    pointer-events: auto;
    display: flex; align-items: center; justify-content: space-between; gap: 16px;
    background: #f2f3f5; color: var(--ink); border-radius: 12px; padding: 12px 16px;
    min-height: 48px; box-shadow: 0 6px 24px rgba(0, 0, 0, 0.35);
    animation: up 160ms ease-out;
  }
  .toast button { color: var(--accent); font-weight: 600; min-height: 44px; padding: 0 8px; }
  @keyframes up { from { transform: translateY(12px); opacity: 0; } to { transform: none; opacity: 1; } }
</style>
