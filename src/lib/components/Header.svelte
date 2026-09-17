<script lang="ts">
  import { ListChecks, Settings } from '@lucide/svelte';
  import { store } from '$lib/data/store.svelte';
  import { fmtHeaderDate, fmtAge } from '$lib/data/format';
  import { openSheet } from '$lib/data/ui.svelte';
</script>

<header class="hdr">
  <div class="avatar" aria-hidden="true">{store.child?.name?.[0] ?? '·'}<span class="rt {store.realtime}" title="sync: {store.realtime}"></span></div>
  <div class="who">
    <h1>{store.child?.name ?? 'Baby'}</h1>
    <div class="muted sub">{fmtHeaderDate(store.now)}{#if store.child}<span class="sep">·</span>{fmtAge(store.child.birth_date, store.now)}{/if}</div>
  </div>
  <div class="actions">
    <button class="pill" onclick={() => openSheet('summary')} aria-label="Summary"><ListChecks size={22} /></button>
    <a class="pill" href="/settings" aria-label="Settings"><Settings size={22} /></a>
  </div>
</header>

<style>
  .hdr {
    display: flex; align-items: center; gap: 16px;
    padding: 16px 20px 12px;
  }
  .avatar {
    width: 64px; height: 64px; border-radius: 50%; flex: none;
    background: linear-gradient(135deg, var(--feed), var(--pump)); color: var(--ink);
    display: grid; place-items: center; font-family: var(--serif); font-size: 32px;
  }
  .avatar { position: relative; }
  .rt { position: absolute; right: 2px; bottom: 2px; width: 12px; height: 12px; border-radius: 50%; border: 2px solid var(--bg); background: #888; }
  .rt.live { background: #5fbf7a; }
  .rt.offline { background: var(--danger); }
  .who { flex: 1; min-width: 0; }
  h1 { font-size: 40px; line-height: 1.05; }
  .sub { font-size: 16px; }
  .sep { white-space: nowrap; }
  .sep { margin: 0 6px; }
  .actions { display: flex; gap: 8px; align-self: flex-end; flex: none; }
  .pill {
    width: 48px; height: 44px; border-radius: 10px; border: 1px solid var(--rule); display: grid; place-items: center;
    color: var(--text);
  }
</style>
