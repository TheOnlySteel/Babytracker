<script lang="ts">
  import { onMount, onDestroy, type Snippet } from 'svelte';
  import { closeSheet } from '$lib/data/ui.svelte';

  /**
   * One modal implementation for sheets and the picker (WAI-ARIA modal dialog pattern):
   * focus moves in and is trapped, the app behind (#app-root) is inert and scroll-locked,
   * Escape / scrim close it, and a dirty form asks before discarding.
   */
  let {
    label,
    dirty = false,
    busy = false,
    variant = 'sheet',
    children
  }: {
    label: string;
    /** the form has unsaved changes: dismissal asks first */
    dirty?: boolean;
    /** a write is in flight: dismissal is refused */
    busy?: boolean;
    variant?: 'sheet' | 'picker';
    children: Snippet;
  } = $props();

  let root: HTMLElement;
  let confirming = $state(false);
  let scrollY = 0;
  const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';
  const focusables = () => Array.from(root?.querySelectorAll<HTMLElement>(FOCUSABLE) ?? []).filter((el) => el.offsetParent !== null);

  /** Ask to close: immediate when nothing is at stake, a confirmation when the form is dirty. */
  export function requestClose() {
    if (busy) return;
    if (dirty && !confirming) {
      confirming = true;
      queueMicrotask(() => root?.querySelector<HTMLElement>('[data-keep]')?.focus());
      return;
    }
    closeSheet();
  }

  function onkeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      e.preventDefault();
      requestClose();
      return;
    }
    if (e.key !== 'Tab' || !root) return;
    const items = focusables();
    if (items.length === 0) return;
    const first = items[0];
    const last = items[items.length - 1];
    const active = document.activeElement as HTMLElement | null;
    if (e.shiftKey && (active === first || !root.contains(active))) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && (active === last || !root.contains(active))) {
      e.preventDefault();
      first.focus();
    }
  }

  onMount(() => {
    // Inert the real app subtree (not body's wrapper, which also contains this modal).
    document.getElementById('app-root')?.setAttribute('inert', '');
    // Lock background scrolling, iOS included: fix the body at its current scroll offset.
    scrollY = window.scrollY;
    document.body.style.position = 'fixed';
    document.body.style.top = `-${scrollY}px`;
    document.body.style.left = '0';
    document.body.style.right = '0';
    document.body.style.width = '100%';
    const items = focusables();
    (root.querySelector<HTMLElement>('[data-autofocus]') ?? items[0])?.focus({ preventScroll: true });
  });
  onDestroy(() => {
    document.getElementById('app-root')?.removeAttribute('inert');
    document.body.style.position = '';
    document.body.style.top = '';
    document.body.style.left = '';
    document.body.style.right = '';
    document.body.style.width = '';
    window.scrollTo(0, scrollY);
  });
</script>

<svelte:window {onkeydown} />

<div class="scrim" role="presentation" onclick={requestClose} ontouchmove={(e) => e.preventDefault()}></div>
<div class="modal {variant}" role="dialog" aria-modal="true" aria-label={label} bind:this={root}>
  {@render children()}
  {#if confirming}
    <div class="confirm" role="alertdialog" aria-label="Discard changes?">
      <p>Discard what you've entered?</p>
      <div class="actions">
        <button class="btn-primary" data-keep onclick={() => (confirming = false)}>Keep editing</button>
        <button class="btn-ghost" onclick={closeSheet}>Discard</button>
      </div>
    </div>
  {/if}
</div>

<style>
  .scrim {
    position: fixed; inset: 0; background: rgba(0, 0, 0, 0.45); z-index: 40; touch-action: none;
    animation: fade 150ms ease-out;
  }
  .modal {
    position: fixed; left: 0; right: 0; bottom: 0; z-index: 41;
    max-height: calc(100dvh - var(--safe-t) - 56px);
    display: flex; flex-direction: column;
    background: var(--card); border-radius: 20px 20px 0 0; overflow: hidden;
    padding-left: var(--safe-l); padding-right: var(--safe-r);
    animation: rise 220ms cubic-bezier(0.2, 0.8, 0.2, 1);
    box-shadow: 0 -8px 40px rgba(0, 0, 0, 0.4);
  }
  .modal.picker { background: #2a3142; }
  .confirm {
    position: absolute; inset: 0; z-index: 2; background: rgba(27, 32, 48, 0.92);
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 16px; padding: 24px;
  }
  .confirm p { margin: 0; font-family: var(--serif); font-size: 24px; text-align: center; }
  .actions { display: flex; flex-direction: column; gap: 10px; width: min(100%, 320px); }
  @keyframes rise { from { transform: translateY(100%); } to { transform: translateY(0); } }
  @keyframes fade { from { opacity: 0; } to { opacity: 1; } }
  @media (prefers-reduced-motion: reduce) {
    .scrim, .modal { animation: none; }
  }
</style>
