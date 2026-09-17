<script lang="ts">
  import { untrack } from 'svelte';
  import { ChevronDown, ChevronUp } from '@lucide/svelte';
  import type { DiaperColor, DiaperPayload, Entry, Texture } from '$lib/data/types';
  import { TEXTURES, DIAPER_COLORS, DIAPER_SWATCH } from '$lib/data/types';
  import { store, ConflictError } from '$lib/data/store.svelte';
  import { closeSheet, openSheet } from '$lib/data/ui.svelte';
  import { lastOf } from '$lib/data/derive';
  import Sheet from '$lib/ui/Sheet.svelte';
  import ConflictBar from '$lib/ui/ConflictBar.svelte';
  import TimeRow from '$lib/ui/TimeRow.svelte';
  import NoteRow from '$lib/ui/NoteRow.svelte';

  let { entry }: { entry?: Entry } = $props();
  const editing = untrack(() => (entry?.type === 'diaper' ? entry : undefined));
  const p = editing?.payload as DiaperPayload | undefined;
  const lastDirty = lastOf(store.entries, (e) => e.type === 'diaper' && (e.payload as DiaperPayload).dirty)?.payload as DiaperPayload | undefined;

  let startedAt = $state(editing ? new Date(editing.started_at) : new Date());
  let wet = $state(p?.wet ?? true);
  let dirty = $state(p?.dirty ?? false);
  let dry = $state(p?.dry ?? false);
  let texture = $state<Texture[]>(p?.texture ?? lastDirty?.texture ?? []);
  let color = $state<DiaperColor[]>(p?.color ?? lastDirty?.color ?? []);
  let blowout = $state(p?.blowout ?? false);
  let rash = $state(p?.rash ?? false);
  let note = $state(editing?.note ?? '');
  let detailOpen = $state(p?.dirty ?? false);
  let saving = $state(false);
  let version = $state(editing?.updated_at);
  let conflict = $state<Entry | null>(null);
  const snapshot = () => JSON.stringify([startedAt.getTime(), wet, dirty, dry, texture, color, blowout, rash, note]);
  const initial = snapshot();
  const isDirty = $derived(snapshot() !== initial);

  function setKind(k: 'wet' | 'dirty' | 'dry') {
    if (k === 'dry') {
      dry = !dry;
      if (dry) {
        wet = false;
        dirty = false;
      }
    } else {
      dry = false;
      if (k === 'wet') wet = !wet;
      else {
        dirty = !dirty;
        if (dirty) detailOpen = true;
      }
    }
    if (!wet && !dirty && !dry) wet = true;
  }
  function toggleIn<T>(arr: T[], v: T): T[] {
    return arr.includes(v) ? arr.filter((x) => x !== v) : [...arr, v];
  }

  async function save() {
    const payload: DiaperPayload = { wet, dirty, dry, texture: dirty ? [...texture] : [], color: dirty ? [...color] : [], blowout, rash };
    saving = true;
    try {
      if (editing) await store.update(editing.id, { started_at: startedAt.toISOString(), payload, note: note || null }, { undoLabel: 'Updated diaper', expectedUpdatedAt: version });
      else await store.insert({ type: 'diaper', started_at: startedAt, payload, note: note || null }, { undoLabel: `Logged ${wet && dirty ? 'wet + dirty' : dirty ? 'dirty' : wet ? 'wet' : 'dry'} diaper` });
      closeSheet();
    } catch (e) {
      if (e instanceof ConflictError && e.latest) conflict = e.latest;
    } finally {
      saving = false;
    }
  }
  async function del() {
    if (!editing) return;
    await store.remove(editing.id, 'Diaper deleted', version);
    closeSheet();
  }
</script>

<Sheet title="Diaper" color="var(--diaper)" dark onsave={save} {saving} dirty={isDirty}>
  {#if conflict}
    <ConflictBar latest={conflict} onUseTheirs={() => openSheet('diaper', conflict!)} onKeepMine={() => { version = conflict!.updated_at; conflict = null; save(); }} />
  {/if}
  <TimeRow label="Time" bind:value={startedAt} />

  <div class="circles">
    <button class="circle" aria-pressed={wet} onclick={() => setKind('wet')}>wet</button>
    <button class="circle" aria-pressed={dirty} onclick={() => setKind('dirty')}>dirty</button>
    <button class="circle" aria-pressed={dry} onclick={() => setKind('dry')}>dry</button>
  </div>

  <button class="row full" onclick={() => (detailOpen = !detailOpen)} aria-expanded={detailOpen}>
    <span class="row-label">Texture &amp; Color</span>
    {#if detailOpen}<ChevronUp size={22} />{:else}<ChevronDown size={22} />{/if}
  </button>
  {#if detailOpen}
    <div class="detail">
      <div class="grid5">
        {#each TEXTURES as t}
          <button class="opt" aria-pressed={texture.includes(t)} onclick={() => (texture = toggleIn(texture, t))}>
            <span class="blob {t}"></span>
            <span class="cap">{t}</span>
          </button>
        {/each}
      </div>
      <div class="grid6">
        {#each DIAPER_COLORS as c}
          <button class="opt" aria-pressed={color.includes(c)} onclick={() => (color = toggleIn(color, c))}>
            <span class="swatch" style:background={DIAPER_SWATCH[c]}></span>
            <span class="cap">{c}</span>
          </button>
        {/each}
      </div>
    </div>
  {/if}

  <div class="row">
    <span class="row-label">Blowout</span>
    <button class="switch" role="switch" aria-checked={blowout} onclick={() => (blowout = !blowout)} aria-label="Blowout"></button>
  </div>
  <div class="row">
    <span class="row-label">Diaper Rash</span>
    <button class="switch" role="switch" aria-checked={rash} onclick={() => (rash = !rash)} aria-label="Diaper rash"></button>
  </div>

  <NoteRow bind:value={note} />

  {#snippet footer()}
    {#if editing}
      <button class="btn-link danger" onclick={del} disabled={saving}>Delete</button>
    {/if}
  {/snippet}
</Sheet>

<style>
  .full { width: 100%; text-align: left; }
  .detail { padding: 8px 12px 16px; border-bottom: 1px solid var(--rule); display: flex; flex-direction: column; gap: 16px; }
  .grid5, .grid6 { display: grid; gap: 4px; }
  .grid5 { grid-template-columns: repeat(5, 1fr); }
  .grid6 { grid-template-columns: repeat(6, 1fr); }
  .opt { display: flex; flex-direction: column; align-items: center; gap: 6px; padding: 8px 2px; border-radius: 12px; min-height: 48px; }
  .opt[aria-pressed='true'] { background: var(--accent-soft); box-shadow: inset 0 0 0 1.5px var(--accent-text); }
  .cap { font-size: 15px; text-transform: capitalize; }
  .swatch { width: 44px; height: 44px; border-radius: 45% 55% 50% 50% / 55% 45% 55% 45%; }
  .blob { width: 44px; height: 44px; background: var(--diaper); border-radius: 50%; }
  .blob.runny { border-radius: 60% 40% 55% 45% / 50% 60% 40% 50%; transform: scaleX(1.15); }
  .blob.mucousy { border-radius: 50% 50% 30% 30%; height: 48px; }
  .blob.mushy { border-radius: 55% 45% 50% 50% / 45% 55% 45% 55%; }
  .blob.solid { border-radius: 50% 50% 20% 20%; width: 40px; }
  .blob.pebbles { background: radial-gradient(circle at 30% 30%, var(--diaper) 8px, transparent 9px), radial-gradient(circle at 70% 35%, var(--diaper) 7px, transparent 8px), radial-gradient(circle at 45% 70%, var(--diaper) 9px, transparent 10px); }
</style>
