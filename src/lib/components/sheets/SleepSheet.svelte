<script lang="ts">
  import { untrack } from "svelte";
  import { store, ConflictError } from "$lib/data/store.svelte";
  import type { Entry, SleepPayload, SleepKind } from "$lib/data/types";
  import { closeSheet, openSheet } from "$lib/data/ui.svelte";
  import { sleepTooLong, notInFuture } from "$lib/data/validate";
  import { sleepKind, localParts } from "$lib/data/sleep-time";
  import { sleepElapsedS } from "$lib/data/derive";
  import { fmtDuration } from "$lib/data/format";
  import Sheet from "$lib/ui/Sheet.svelte";
  import TimeRow from "$lib/ui/TimeRow.svelte";
  import NoteRow from "$lib/ui/NoteRow.svelte";
  import ConflictBar from "$lib/ui/ConflictBar.svelte";
  let { entry }: { entry?: Entry } = $props();
  const editing = untrack(() => entry);
  let base = $state(editing);
  const p0 = editing?.payload as SleepPayload | undefined;
  let kind = $state<SleepKind>(p0?.kind ?? "nap"),
    place = $state<SleepPayload["place"]>(p0?.place ?? "crib");
  let start = $state(editing ? new Date(editing.started_at) : new Date()),
    end = $state(editing?.ended_at ? new Date(editing.ended_at) : new Date());
  let still = $state(editing ? !editing.ended_at : true),
    earlier = $state(false),
    note = $state(editing?.note ?? "");
  let saving = $state(false),
    error = $state(""),
    conflict = $state<Entry | null>(null);
  const snapshot = () =>
    JSON.stringify([kind, place, +start, +end, still, earlier, note]);
  const initial = snapshot();
  const dirty = $derived(snapshot() !== initial);
  const auto = p0?.source === "cradlewise";
  function previousNap() {
    earlier = true;
    still = false;
    end = new Date();
    start = new Date(+end - 3600000);
  }
  async function save(forceEnd = false) {
    error = "";
    const finish = forceEnd ? new Date() : end;
    const running = forceEnd ? false : still;
    if (!Number.isFinite(+start) || (!running && !Number.isFinite(+finish)))
      return void (error = "Choose valid times");
    if (!running && +finish < +start)
      return void (error = "End must be after start");
    error =
      notInFuture(start, "Start") ??
      (!running ? (notInFuture(finish, "End") ?? "") : "");
    if (error) return;
    const timesChanged =
      !!editing &&
      (+start !== Date.parse(editing.started_at) ||
        running !== !editing.ended_at ||
        (!running && +finish !== Date.parse(editing.ended_at ?? "")));
    if (
      (!editing || timesChanged) &&
      sleepTooLong(start, running ? new Date() : finish) &&
      !confirm("This sleep is over 16 hours. Save these times?")
    )
      return;
    const p: SleepPayload = {
      ...(base?.payload as SleepPayload),
      kind,
      place,
      source: p0?.source ?? "manual",
    };
    if (timesChanged || forceEnd) p.timing_locked = true;
    if (kind === "night") {
      // The household zone decides the night; the device zone stands in until it is known.
      const tz = store.timezone ?? Intl.DateTimeFormat().resolvedOptions().timeZone;
      p.night_key =
        sleepKind(
          start.toISOString(),
          tz,
          store.sleepStatus?.bed_min ?? 1200,
          store.sleepStatus?.rise_min ?? 480,
        ).night_key ?? localParts(start, tz).date;
    }
    else delete p.night_key;
    saving = true;
    try {
      if (editing) {
        // Annotation-only edits never send stale times back to the poller.
        await store.update(
          editing.id,
          {
            payload: p,
            note: note || null,
            ...(timesChanged || forceEnd
              ? {
                  started_at: start.toISOString(),
                  ended_at: running ? null : finish.toISOString(),
                }
              : {}),
          },
          { expectedUpdatedAt: base?.updated_at, undoLabel: "Updated sleep" },
        );
      } else
        await store.insert(
          {
            type: "sleep",
            started_at: start,
            ended_at: running ? null : finish,
            payload: p,
            note: note || null,
          },
          { undoLabel: running ? "Sleep started" : "Sleep logged" },
        );
      closeSheet();
    } catch (e) {
      if (e instanceof ConflictError && e.latest) conflict = e.latest;
      else {
        error =
          (e as { code?: string }).code === "23505"
            ? "A sleep is already running. Open it below."
            : "Could not save. Your changes are still here.";
      }
    } finally {
      saving = false;
    }
  }
  async function del() {
    if (editing) {
      await store.remove(
        editing.id,
        auto ? "Sleep dismissed" : "Sleep deleted",
        base?.updated_at,
      );
      closeSheet();
    }
  }
</script>

<Sheet
  title="Sleep"
  color="var(--sleep)"
  dark
  onsave={editing || earlier ? () => save() : undefined}
  {saving}
  {dirty}
>
  <p class="status">
    {auto
      ? `Cradlewise${p0?.timing_locked ? " · adjusted" : p0?.provisional ? " · provisional" : ""}`
      : "Manual"}{#if editing && !editing.ended_at}
      · {fmtDuration(sleepElapsedS(editing, store.now))}{/if}
  </p>
  {#if conflict}<ConflictBar
      latest={conflict}
      onUseTheirs={() => openSheet("sleep", conflict!)}
      onKeepMine={() => {
        base = conflict!;
        conflict = null;
        save();
      }}
    />{/if}
  <div class="chips">
    {#each ["nap", "night"] as k}<button
        class="chip"
        aria-pressed={kind === k}
        onclick={() => (kind = k as SleepKind)}
        >{k === "nap" ? "Nap" : "Night"}</button
      >{/each}
  </div>
  <div class="chips">
    {#each ["crib", "stroller", "car", "arms", "other"] as p}<button
        class="chip"
        aria-pressed={place === p}
        onclick={() => (place = p as SleepPayload["place"])}>{p}</button
      >{/each}
  </div>
  <TimeRow bind:value={start} />
  {#if editing || earlier}<label class="row"
      ><span>Still sleeping</span><input
        type="checkbox"
        bind:checked={still}
      /></label
    >{/if}
  {#if !still}<TimeRow label="End Time" bind:value={end} />{/if}
  <NoteRow bind:value={note} />
  {#if error}<p class="error" role="alert">{error}</p>
    {#each store.entries.filter((e) => e.type === "sleep" && !e.ended_at) as e}<button
        class="btn-ghost"
        onclick={() => openSheet("sleep", e)}>Open running sleep</button
      >{/each}{/if}
  {#snippet footer()}
    {#if !editing && !earlier}<button
        class="btn-primary"
        disabled={saving}
        onclick={() => save()}>Start sleep</button
      ><button class="btn-link" onclick={previousNap}>Log an earlier nap</button
      >{/if}
    {#if editing && !editing.ended_at}<button
        class="btn-primary"
        disabled={saving}
        onclick={() => save(true)}>{auto ? "End now" : "Stop"}</button
      >{/if}
    {#if editing}<button class="btn-link danger" disabled={saving} onclick={del}
        >{auto ? "Not a nap" : "Delete"}</button
      >{/if}
  {/snippet}
</Sheet>

<style>
  .chips {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    padding: 8px 20px;
  }
  .status {
    padding: 12px 20px;
    color: var(--muted);
  }
  .error {
    padding: 12px 20px;
    color: var(--danger);
  }
</style>
