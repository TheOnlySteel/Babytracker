<script lang="ts">
  import '../app.css';
  import { onMount } from 'svelte';
  import { configured } from '$lib/supabase';
  import { store } from '$lib/data/store.svelte';
  import SignIn from '$lib/components/SignIn.svelte';
  import TabBar from '$lib/ui/TabBar.svelte';
  import Toasts from '$lib/ui/Toasts.svelte';
  import SheetHost from '$lib/components/SheetHost.svelte';
  import TimerBanner from '$lib/components/TimerBanner.svelte';

  let { children } = $props();
  onMount(() => {
    if (configured) store.init();
  });
</script>

<svelte:head><title>Rosalie</title></svelte:head>

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
{:else if store.error}
  <main class="msg">
    <h1>Something broke</h1>
    <p class="err">{store.error}</p>
    <button class="btn-ghost" onclick={() => location.reload()}>Reload</button>
  </main>
{:else if !store.loaded}
  <main class="msg"><p class="muted">Loading…</p></main>
{:else}
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
</style>
