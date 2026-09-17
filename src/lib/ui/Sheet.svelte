<script lang="ts">
  import type { Snippet } from 'svelte';
  import { X } from '@lucide/svelte';
  import Modal from './Modal.svelte';

  let {
    title,
    color = 'var(--card)',
    dark = false,
    onsave,
    saving = false,
    dirty = false,
    children,
    footer
  }: {
    title: string;
    color?: string;
    /** dark text on the title bar (for pale type colours) */
    dark?: boolean;
    onsave?: () => void;
    saving?: boolean;
    /** unsaved changes: closing asks first */
    dirty?: boolean;
    children: Snippet;
    /** secondary actions (Delete); rendered beside the bottom Save */
    footer?: Snippet;
  } = $props();

  let modal: Modal;
</script>

<Modal label={title} {dirty} busy={saving} bind:this={modal}>
  <header class="bar" style:background={color} class:dark>
    <button class="icon" onclick={() => modal.requestClose()} aria-label="Close"><X size={28} /></button>
    <h2>{title}</h2>
    {#if onsave}
      <button class="save" onclick={onsave} disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
    {:else}
      <span class="save"></span>
    {/if}
  </header>
  <div class="body">
    {@render children()}
  </div>
  {#if onsave || footer}
    <div class="foot">
      {#if onsave}
        <!-- Bottom Save for one-handed reach; the top one stays for iOS familiarity. -->
        <button class="btn-primary big" onclick={onsave} disabled={saving} data-autofocus>{saving ? 'Saving…' : 'Save'}</button>
      {/if}
      {#if footer}{@render footer()}{/if}
    </div>
  {/if}
</Modal>

<style>
  .bar {
    display: grid; grid-template-columns: 56px 1fr 88px; align-items: center;
    min-height: 72px; color: var(--text); flex: none;
  }
  .bar.dark { color: var(--ink); }
  .bar h2 { text-align: center; font-size: 30px; font-weight: 500; }
  .icon { width: 56px; height: 72px; display: grid; place-items: center; }
  .save { font-weight: 600; font-size: 18px; text-align: right; padding-right: 20px; height: 72px; white-space: nowrap; }
  .save:disabled { opacity: 0.6; }
  .body { overflow-y: auto; -webkit-overflow-scrolling: touch; }
  .foot {
    flex: none; display: flex; align-items: center; gap: 12px;
    padding: 12px 20px calc(var(--safe-b) + 12px); border-top: 1px solid var(--rule);
  }
  .big { flex: 1; min-height: 52px; font-size: 18px; }
  .big:disabled { opacity: 0.6; }
</style>
