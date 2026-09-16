<script lang="ts">
  import type { Snippet } from 'svelte';
  import { X } from '@lucide/svelte';
  import { closeSheet } from '$lib/data/ui.svelte';

  let {
    title,
    color = 'var(--card)',
    dark = false,
    onsave,
    saving = false,
    children,
    footer
  }: {
    title: string;
    color?: string;
    /** dark text on the title bar (for pale type colours) */
    dark?: boolean;
    onsave?: () => void;
    saving?: boolean;
    children: Snippet;
    footer?: Snippet;
  } = $props();

  function onkeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') closeSheet();
  }
</script>

<svelte:window {onkeydown} />

<div class="scrim" role="presentation" onclick={closeSheet}></div>
<div class="sheet" role="dialog" aria-modal="true" aria-label={title}>
  <header class="bar" style:background={color} class:dark>
    <button class="icon" onclick={closeSheet} aria-label="Close"><X size={28} /></button>
    <h2>{title}</h2>
    {#if onsave}
      <button class="save" onclick={onsave} disabled={saving}>Save</button>
    {:else}
      <span class="save"></span>
    {/if}
  </header>
  <div class="body">
    {@render children()}
  </div>
  {#if footer}
    <div class="foot">{@render footer()}</div>
  {/if}
</div>

<style>
  .scrim {
    position: fixed; inset: 0; background: rgba(0, 0, 0, 0.45); z-index: 40;
    animation: fade 150ms ease-out;
  }
  .sheet {
    position: fixed; left: 0; right: 0; bottom: 0; z-index: 41;
    max-height: calc(100dvh - var(--safe-t) - 56px);
    display: flex; flex-direction: column;
    background: var(--card); border-radius: 20px 20px 0 0; overflow: hidden;
    animation: rise 220ms cubic-bezier(0.2, 0.8, 0.2, 1);
    box-shadow: 0 -8px 40px rgba(0, 0, 0, 0.4);
  }
  .bar {
    display: grid; grid-template-columns: 56px 1fr 72px; align-items: center;
    min-height: 72px; color: var(--text); flex: none;
  }
  .bar.dark { color: var(--ink); }
  .bar h2 { text-align: center; font-size: 30px; font-weight: 500; }
  .icon { width: 56px; height: 72px; display: grid; place-items: center; }
  .save { font-weight: 600; font-size: 18px; text-align: right; padding-right: 20px; height: 72px; }
  .save:disabled { opacity: 0.5; }
  .body { overflow-y: auto; -webkit-overflow-scrolling: touch; padding-bottom: calc(var(--safe-b) + 16px); }
  .foot { flex: none; padding: 12px 20px calc(var(--safe-b) + 12px); border-top: 1px solid var(--rule); }
  @keyframes rise { from { transform: translateY(100%); } to { transform: translateY(0); } }
  @keyframes fade { from { opacity: 0; } to { opacity: 1; } }
</style>
