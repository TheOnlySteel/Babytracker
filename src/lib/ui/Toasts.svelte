<script lang="ts">
  import { X } from '@lucide/svelte';
  import { toasts, dismiss } from '$lib/data/toast.svelte';
</script>

<div class="toasts">
  {#each toasts.list as t (t.id)}
    <div class="toast" class:error={t.kind === 'error'} role={t.kind === 'error' ? 'alert' : 'status'}>
      <span class="msg">{t.message}</span>
      {#if t.undo}
        <button
          class="undo"
          onclick={async () => {
            dismiss(t.id);
            await t.undo?.();
          }}>{t.action ?? 'Undo'}</button
        >
      {/if}
      {#if t.ttl === 0}
        <button class="x" onclick={() => dismiss(t.id)} aria-label="Dismiss"><X size={20} /></button>
      {/if}
    </div>
  {/each}
</div>

<style>
  .toasts {
    position: fixed; left: calc(16px + var(--safe-l)); right: calc(16px + var(--safe-r)); bottom: calc(var(--tab-h) + var(--safe-b) + 12px);
    display: flex; flex-direction: column; gap: 8px; z-index: 60; pointer-events: none;
  }
  .toast {
    pointer-events: auto;
    display: flex; align-items: center; gap: 12px;
    background: #f2f3f5; color: var(--ink); border-radius: 12px; padding: 10px 8px 10px 16px;
    min-height: 52px; box-shadow: 0 6px 24px rgba(0, 0, 0, 0.35);
    animation: up 160ms ease-out;
  }
  .toast.error { background: #ffe3e4; border: 1.5px solid var(--danger); }
  .msg { flex: 1; }
  .undo { color: var(--accent); font-weight: 600; min-height: 44px; padding: 0 12px; }
  .x { width: 44px; height: 44px; display: grid; place-items: center; border-radius: 50%; color: var(--ink); }
  @keyframes up { from { transform: translateY(12px); opacity: 0; } to { transform: none; opacity: 1; } }
  @media (prefers-reduced-motion: reduce) { .toast { animation: none; } }
</style>
