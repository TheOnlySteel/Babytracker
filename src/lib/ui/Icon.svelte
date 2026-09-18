<script lang="ts">
  // Simple two-tone line icons in the type colour. Deliberately not Nara's illustrations.
  let { kind, size = 48, color }: { kind: 'bottle' | 'breast' | 'diaper' | 'pump' | 'growth' | 'combo' | 'sleep' | 'unknown'; size?: number; color?: string } = $props();
  const fill = $derived(color ?? { bottle: 'var(--feed)', breast: 'var(--feed)', combo: 'var(--feed)', diaper: 'var(--diaper)', pump: 'var(--pump)', growth: 'var(--growth)', sleep: 'var(--sleep)', unknown: 'var(--muted)' }[kind]);
</script>

<svg width={size} height={size} viewBox="0 0 48 48" fill="none" aria-hidden="true">
  {#if kind === 'bottle' || kind === 'combo'}
    <ellipse cx="22" cy="31" rx="12" ry="11" fill={fill} opacity="0.85" />
    <path d="M20 8h8v3.5a3 3 0 0 1 2 2.8V16h1v6h-1v14a5 5 0 0 1-5 5h-2a5 5 0 0 1-5-5V22h-1v-6h1v-1.7a3 3 0 0 1 2-2.8V8Z" stroke="#fff" stroke-width="2.4" stroke-linejoin="round" />
    <path d="M22 8v4M26 8v4" stroke="#fff" stroke-width="2.4" stroke-linecap="round" />
    {#if kind === 'combo'}
      <circle cx="12" cy="20" r="7" fill={fill} opacity="0.85" stroke="#fff" stroke-width="2.4" />
    {/if}
  {:else if kind === 'breast'}
    <path d="M27 12c-8 0-13 6-13 13 0 8 6 12 13 12s12-5 12-12" fill={fill} opacity="0.85" />
    <path d="M12 30c2 7 8 10 15 10 8 0 13-6 13-13" stroke="#fff" stroke-width="2.4" stroke-linecap="round" />
    <circle cx="28" cy="27" r="2.2" fill="#fff" />
  {:else if kind === 'diaper'}
    <ellipse cx="22" cy="30" rx="13" ry="10" fill={fill} opacity="0.75" />
    <path d="M9 16h30v6c0 9-6 16-15 16S9 31 9 22v-6Z" stroke="#fff" stroke-width="2.4" stroke-linejoin="round" />
    <path d="M9 16c4 2 8 4 8 9M39 16c-4 2-8 4-8 9" stroke="#fff" stroke-width="2.4" stroke-linecap="round" />
    <path d="M14 21h20" stroke="#fff" stroke-width="2.4" stroke-linecap="round" />
  {:else if kind === 'pump'}
    <ellipse cx="25" cy="30" rx="12" ry="10" fill={fill} opacity="0.85" />
    <path d="M22 8 8 34h28L22 8Z" stroke="#fff" stroke-width="2.4" stroke-linejoin="round" />
    <path d="M28 30l7 5-3 4-7-5" stroke="#fff" stroke-width="2.4" stroke-linejoin="round" />
  {:else if kind === 'sleep'}
    <path d="M32 8a16 16 0 1 0 8 27A17 17 0 0 1 32 8Z" fill={fill} stroke="#fff" stroke-width="2.4" />
  {:else if kind === 'unknown'}
    <circle cx="24" cy="24" r="15" stroke={fill} stroke-width="2.4" /><path d="M24 15v11m0 5v2" stroke="#fff" stroke-width="2.4" />
  {:else if kind === 'growth'}
    <ellipse cx="24" cy="30" rx="12" ry="10" fill={fill} opacity="0.85" />
    <rect x="12" y="10" width="24" height="28" rx="3" stroke="#fff" stroke-width="2.4" />
    <path d="M18 17h12M18 22h6" stroke="#fff" stroke-width="2.4" stroke-linecap="round" />
  {/if}
</svg>
