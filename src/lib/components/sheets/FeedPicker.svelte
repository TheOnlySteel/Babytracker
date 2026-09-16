<script lang="ts">
  import { openSheet, closeSheet } from '$lib/data/ui.svelte';
  import Icon from '$lib/ui/Icon.svelte';
  function onkeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') closeSheet();
  }
</script>

<svelte:window {onkeydown} />
<div class="scrim" role="presentation" onclick={closeSheet}></div>
<div class="picker" role="dialog" aria-label="Choose feed type">
  <button onclick={() => openSheet('breastfeed')}><Icon kind="breast" size={56} /><span>Breastfeed</span></button>
  <button onclick={() => openSheet('bottle')}><Icon kind="bottle" size={56} /><span>Bottle Feed</span></button>
</div>

<style>
  .scrim { position: fixed; inset: 0; background: rgba(0, 0, 0, 0.45); z-index: 40; }
  .picker {
    position: fixed; left: 0; right: 0; bottom: 0; z-index: 41; background: #2a3142;
    border-radius: 20px 20px 0 0; padding: 16px 0 calc(var(--safe-b) + 16px);
    animation: rise 200ms cubic-bezier(0.2, 0.8, 0.2, 1);
  }
  button { display: flex; align-items: center; gap: 24px; width: 100%; padding: 16px 32px; min-height: 96px; text-align: left; }
  span { font-family: var(--serif); font-size: 30px; }
  @keyframes rise { from { transform: translateY(100%); } to { transform: translateY(0); } }
</style>
