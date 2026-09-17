<script lang="ts">
  import { untrack } from 'svelte';
  import { Plus, ChevronRight } from '@lucide/svelte';
  import type { BottleKind, BottlePayload, Entry } from '$lib/data/types';
  import { store } from '$lib/data/store.svelte';
  import { closeSheet } from '$lib/data/ui.svelte';
  import { recentAmounts, lastOf } from '$lib/data/derive';
  import { nonNegative } from '$lib/data/validate';
  import Sheet from '$lib/ui/Sheet.svelte';
  import TimeRow from '$lib/ui/TimeRow.svelte';
  import NoteRow from '$lib/ui/NoteRow.svelte';

  let { entry }: { entry?: Entry } = $props();
  // Sheets are keyed on open, so the initial entry is the only one this instance ever edits.
  const editing = untrack(() => (entry?.type === 'bottle' ? entry : undefined));

  const lastBottle = lastOf(store.entries, (e) => e.type === 'bottle');
  const lastP = (editing ?? lastBottle)?.payload as BottlePayload | undefined;
  const amounts = recentAmounts(store.entries);
  const DEFAULT_BRANDS = ['Enfamil Neuropro', 'Good Start Plus'];
  let brands = $state<string[]>([...new Set([...(store.prefs.formula_brands ?? DEFAULT_BRANDS)])]);

  let kinds = $state<BottleKind[]>(lastP?.kinds?.length ? [...lastP.kinds] : ['formula']);
  let startedAt = $state(editing ? new Date(editing.started_at) : new Date());
  let brand = $state(lastP?.formula_brand ?? store.prefs.last_formula_brand ?? untrack(() => brands[0]));
  // Mixed bottles prefill both parts from the previous mixed bottle so the form is valid on open.
  const lastMixed = (lastP?.kinds?.length ?? 0) === 2;
  let breastMl = $state<number | null>(editing || lastMixed ? (lastP?.breast_milk_ml ?? null) : null);
  let formulaMl = $state<number | null>(editing || lastMixed ? (lastP?.formula_ml ?? null) : null);
  // Single-kind amount: pre-select the last amount (Nara's "use last amount? yes")
  let amount = $state<number | null>(editing ? (lastP?.breast_milk_ml ?? 0) + (lastP?.formula_ml ?? 0) : (amounts[0] ?? null));
  let note = $state(editing?.note ?? '');
  let showBrands = $state(false);
  let saving = $state(false);
  let err = $state('');

  const both = $derived(kinds.length === 2);
  const hasFormula = $derived(kinds.includes('formula'));

  function toggle(k: BottleKind) {
    const next = kinds.includes(k) ? kinds.filter((x) => x !== k) : [...kinds, k];
    kinds = next.length ? next : [k];
    if (kinds.length === 2 && amount != null && breastMl == null && formulaMl == null) {
      if (k === 'formula') breastMl = amount;
      else formulaMl = amount;
    }
  }

  function addBrand() {
    const name = prompt('Formula brand');
    if (!name?.trim()) return;
    brands = [...brands, name.trim()];
    brand = name.trim();
    store.savePrefs({ formula_brands: brands });
    showBrands = false;
  }

  async function save() {
    err = '';
    const payload: BottlePayload = { kinds: [...kinds] };
    if (both) {
      const b = nonNegative(breastMl, 'Breast milk', { max: 1000 });
      const f = nonNegative(formulaMl, 'Formula', { max: 1000 });
      if (b.error || f.error) return void (err = b.error ?? f.error ?? '');
      payload.breast_milk_ml = b.value ?? 0;
      payload.formula_ml = f.value ?? 0;
    } else {
      const a = nonNegative(amount, 'Amount', { max: 1000 });
      if (a.error) return void (err = a.error);
      if (kinds[0] === 'formula') payload.formula_ml = a.value ?? 0;
      else payload.breast_milk_ml = a.value ?? 0;
    }
    if (hasFormula && brand) payload.formula_brand = brand;
    const total = (payload.breast_milk_ml ?? 0) + (payload.formula_ml ?? 0);
    if (total <= 0) return void (err = 'Enter an amount');
    saving = true;
    try {
      if (editing) await store.update(editing.id, { started_at: startedAt.toISOString(), payload, note: note || null }, { undoLabel: 'Updated bottle', expectedUpdatedAt: editing.updated_at });
      else await store.insert({ type: 'bottle', started_at: startedAt, payload, note: note || null }, { undoLabel: `Logged ${total} mL bottle` });
      if (hasFormula && brand && brand !== store.prefs.last_formula_brand) store.savePrefs({ last_formula_brand: brand });
      closeSheet();
    } catch {
      /* toast shown; keep the form */
    } finally {
      saving = false;
    }
  }

  async function del() {
    if (!editing) return;
    await store.remove(editing.id, 'Bottle deleted', editing.updated_at);
    closeSheet();
  }

  const numVal = (e: Event) => {
    err = '';
    const v = (e.target as HTMLInputElement).value;
    return v === '' ? null : Number(v);
  };
</script>

<Sheet title="Bottle Feed" color="var(--feed)" dark onsave={save} {saving}>
  <div class="circles">
    <button class="circle" aria-pressed={kinds.includes('breast_milk')} onclick={() => toggle('breast_milk')}>breast<br />milk</button>
    <button class="circle" aria-pressed={kinds.includes('formula')} onclick={() => toggle('formula')}>formula</button>
  </div>

  <TimeRow bind:value={startedAt} />

  {#if hasFormula}
    <button class="row full" onclick={() => (showBrands = !showBrands)} aria-expanded={showBrands}>
      <span class="row-label">Formula Brand</span>
      <span class="row-value with-chev">{brand}<ChevronRight size={20} /></span>
    </button>
    {#if showBrands}
      <div class="brands">
        {#each brands as b}
          <button
            class="chip"
            aria-pressed={b === brand}
            onclick={() => {
              brand = b;
              showBrands = false;
            }}>{b}</button
          >
        {/each}
        <button class="chip" onclick={addBrand}><Plus size={16} />Add</button>
      </div>
    {/if}
  {/if}

  {#if both}
    <label class="row">
      <span class="row-label">Breast Milk Amount</span>
      <span class="amt"><input type="number" inputmode="numeric" min="0" value={breastMl ?? ''} oninput={(e) => (breastMl = numVal(e))} /> mL</span>
    </label>
    <label class="row">
      <span class="row-label">Formula Amount</span>
      <span class="amt"><input type="number" inputmode="numeric" min="0" value={formulaMl ?? ''} oninput={(e) => (formulaMl = numVal(e))} /> mL</span>
    </label>
  {:else}
    <label class="row">
      <span class="row-label">{hasFormula ? 'Formula Amount' : 'Amount'}</span>
      <span class="amt"><input type="number" inputmode="numeric" min="0" value={amount ?? ''} oninput={(e) => (amount = numVal(e))} /> mL</span>
    </label>
    {#if amounts.length}
      <div class="chips amounts">
        {#each amounts as a}
          <button class="chip" aria-pressed={amount === a} onclick={() => (amount = a)}>{a} mL</button>
        {/each}
      </div>
    {/if}
  {/if}
  {#if err}<p class="err" role="alert">{err}</p>{/if}

  <NoteRow bind:value={note} />

  {#snippet footer()}
    {#if editing}
      <button class="btn-ghost danger" onclick={del}>Delete</button>
    {/if}
  {/snippet}
</Sheet>

<style>
  .full { width: 100%; text-align: left; }
  .with-chev { display: inline-flex; align-items: center; gap: 4px; }
  .brands { display: flex; flex-wrap: wrap; gap: 8px; padding: 12px 20px; border-bottom: 1px solid var(--rule); }
  .amt { display: inline-flex; align-items: baseline; gap: 6px; font-size: 17px; }
  .amt input { width: 80px; text-align: right; background: var(--card-2); border: 0; border-radius: 8px; padding: 8px 10px; font-size: 20px; outline: none; }
  .amounts { padding: 4px 20px 16px; border-bottom: 1px solid var(--rule); }
  .err { margin: 0; padding: 8px 20px; color: var(--danger); font-size: 15px; }
  .danger { color: var(--danger); border-color: var(--danger); width: 100%; }
</style>
