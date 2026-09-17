<script lang="ts">
  import { page } from '$app/state';
  import { Sparkles, CalendarDays, ListChecks, Settings } from '@lucide/svelte';
  const tabs = [
    { href: '/', label: 'Activity', icon: Sparkles },
    { href: '/history', label: 'History', icon: CalendarDays },
    { href: '/summary', label: 'Summary', icon: ListChecks },
    { href: '/settings', label: 'Account', icon: Settings }
  ];
</script>

<nav class="tabs">
  {#each tabs as t}
    {@const active = page.url.pathname === t.href}
    <a href={t.href} class:active aria-current={active ? 'page' : undefined}>
      <t.icon size={26} strokeWidth={active ? 2.2 : 1.8} />
      <span>{t.label}</span>
    </a>
  {/each}
</nav>

<style>
  .tabs {
    position: fixed; left: 0; right: 0; bottom: 0; z-index: 30;
    height: calc(var(--tab-h) + var(--safe-b)); padding: 0 var(--safe-r) var(--safe-b) var(--safe-l);
    background: rgba(27, 32, 48, 0.94); backdrop-filter: blur(12px);
    border-top: 1px solid var(--rule);
    display: grid; grid-auto-flow: column; grid-auto-columns: 1fr;
  }
  a {
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
    color: var(--muted); text-decoration: none; font-size: 13px;
  }
  a.active { color: #6f93c8; }
</style>
