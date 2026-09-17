<script lang="ts">
  import { X } from '@lucide/svelte';
  import { openSheet } from '$lib/data/ui.svelte';
  import Modal from '$lib/ui/Modal.svelte';
  import Icon from '$lib/ui/Icon.svelte';
  let modal: Modal;
</script>

<Modal label="Choose feed type" variant="picker" bind:this={modal}>
  <div class="head">
    <span class="title">Log a feed</span>
    <button class="close" onclick={() => modal.requestClose()} aria-label="Close"><X size={26} /></button>
  </div>
  <button class="opt" onclick={() => openSheet('breastfeed')} data-autofocus><Icon kind="breast" size={56} /><span>Breastfeed</span></button>
  <button class="opt" onclick={() => openSheet('bottle')}><Icon kind="bottle" size={56} /><span>Bottle Feed</span></button>
  <div class="pad"></div>
</Modal>

<style>
  .head { display: flex; align-items: center; justify-content: space-between; padding: 12px 12px 4px 32px; }
  .title { font-size: 15px; color: var(--muted); text-transform: uppercase; letter-spacing: 0.06em; }
  .close { width: 48px; height: 48px; display: grid; place-items: center; border-radius: 50%; color: var(--text); }
  .opt { display: flex; align-items: center; gap: 24px; width: 100%; padding: 16px 32px; min-height: 96px; text-align: left; }
  .opt span { font-family: var(--serif); font-size: 30px; }
  .pad { height: calc(var(--safe-b) + 8px); }
</style>
