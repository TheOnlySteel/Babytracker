<script lang="ts">
  import '../app.css';
  import { onMount } from 'svelte';
  import { pwaInfo } from 'virtual:pwa-info';
  import { configured } from '$lib/supabase';
  import { store } from '$lib/data/store.svelte';
  import SignIn from '$lib/components/SignIn.svelte';
  import TabBar from '$lib/ui/TabBar.svelte';
  import Toasts from '$lib/ui/Toasts.svelte';
  import SheetHost from '$lib/components/SheetHost.svelte';
  import TimerBanner from '$lib/components/TimerBanner.svelte';

  let { children } = $props();
  const webManifestLink = $derived(pwaInfo ? pwaInfo.webManifest.linkTag : '');

  onMount(async () => {
    if (configured) store.init();
    if (pwaInfo) {
      const { registerSW } = await import('virtual:pwa-register');
      registerSW({ immediate: true });
    }
  });
</script>

<svelte:head>
  <title>Rosalie</title>
  {@html webManifestLink}
</svelte:head>

{#if !configured}
  <main class="msg">
    <h1>Not configured</h1>
    <p>Set <code>PUBLIC_SUPABASE_URL</code> and <code>PUBLIC_SUPABASE_ANON_KEY</code> in the build environment. See SETUP.md.</p>
  </main>
{:else if !store.ready}
  <main class="msg"><p class="muted">Loading…</p></main>
{:else if !store.session}
  <SignIn />
{:else if store.error === 'not-in-household'}
  <main class="msg">
    <h1>Almost there</h1>
    <p>You're signed in as <strong>{store.session.user.email}</strong> but aren't in a household yet.</p>
    <p class="muted">Run <code>supabase/seed.sql</code> with this email, then reload.</p>
    <p class="muted small">user id: <code>{store.session.user.id}</code></p>
    <button class="btn-ghost" onclick={() => location.reload()}>Reload</button>
    <button class="btn-ghost" onclick={() => store.signOut()}>Sign out</button>
  </main>
{:else if store.error && !store.loaded}
  <main class="msg">
    <h1>Can't load</h1>
    <p class="err">{store.error}</p>
    <button class="btn-primary" onclick={() => store.loadHousehold()}>Try again</button>
    <button class="btn-ghost" onclick={() => store.signOut()}>Sign out</button>
  </main>
{:else if !store.loaded}
  <main class="msg"><p class="muted">Loading…</p></main>
{:else}
  {#if store.syncError}
    <div class="sync" role="status">
      <span>Can't reach the server. Showing what was loaded last.</span>
      <button onclick={() => store.refreshEntries()}>Retry</button>
    </div>
  {/if}
  <TimerBanner />
  {@render children()}
  <TabBar />
  <SheetHost />
{/if}
<Toasts />

<style>
  .msg { padding: calc(var(--safe-t) + 80px) 24px; max-width: 480px; margin: 0 auto; display: flex; flex-direction: column; gap: 12px; }
  .msg h1 { font-size: 36px; }
  .small { font-size: 13px; word-break: break-all; }
  .err { color: var(--danger); }
  code { background: var(--card); padding: 2px 6px; border-radius: 6px; font-size: 0.9em; }
  .sync {
    position: sticky; top: 0; z-index: 21; display: flex; align-items: center; justify-content: space-between; gap: 12px;
    padding: calc(var(--safe-t) + 8px) 16px 8px; background: var(--danger); color: #fff; font-size: 14px; font-weight: 600;
  }
  .sync button { min-height: 36px; padding: 0 12px; border: 1.5px solid #fff; border-radius: 18px; color: #fff; }
</style>
