<script lang="ts">
  import { untrack } from "svelte";
  import type { Entry, PumpPayload } from "$lib/data/types";
  import { store, ConflictError } from "$lib/data/store.svelte";
  import { closeSheet, openSheet } from "$lib/data/ui.svelte";
  import { nonNegative, notInFuture } from "$lib/data/validate";
  import { sleepElapsedS } from "$lib/data/derive";
  import { stopPatch } from "$lib/data/timers";
  import { fmtDuration } from "$lib/data/format";
  import Sheet from "$lib/ui/Sheet.svelte";
  import ConflictBar from "$lib/ui/ConflictBar.svelte";
  import TimeRow from "$lib/ui/TimeRow.svelte";
  import NoteRow from "$lib/ui/NoteRow.svelte";
  let { entry }: { entry?: Entry } = $props();
  const editing = untrack(() => (entry?.type === "pump" ? entry : undefined));
  let base = $state(editing);
  const p0 = editing?.payload as PumpPayload | undefined;
  const running = !!editing && !editing.ended_at;
  let manual = $state(!!editing && !running),
    startedAt = $state(editing ? new Date(editing.started_at) : new Date());
  let minutes = $state<number | undefined>(
    editing?.ended_at
      ? Math.round((Date.parse(editing.ended_at) - Date.parse(editing.started_at)) / 60000)
      : 20,
  );
  let total = $state<number | undefined>(p0?.total_ml),
    left = $state<number | undefined>(p0?.left_ml),
    right = $state<number | undefined>(p0?.right_ml);
  let split = $state(
      p0?.total_ml == null && (p0?.left_ml != null || p0?.right_ml != null),
    ),
    note = $state(editing?.note ?? "");
  let saving = $state(false),
    err = $state(""),
    conflict = $state<Entry | null>(null);
  const snapshot = () =>
    JSON.stringify([
      +startedAt,
      minutes,
      total,
      left,
      right,
      split,
      note,
    ]);
  const initial = snapshot();
  const dirty = $derived(snapshot() !== initial);
  async function save(action: "start" | "stop" | "save" = "save") {
    err = notInFuture(startedAt, "Start") ?? "";
    if (err) return;
    const m = nonNegative(minutes, "Duration", { max: 1440 }),
      l = nonNegative(left, "Left", { max: 1000 }),
      r = nonNegative(right, "Right", { max: 1000 }),
      t = nonNegative(total, "Total", { max: 2000 });
    err =
      (action === "save" ? m.error : undefined) ??
      (split ? (l.error ?? r.error) : t.error) ??
      "";
    if (err) return;
    const payload: PumpPayload = { ...(base?.payload as PumpPayload) };
    delete payload.total_ml;
    delete payload.left_ml;
    delete payload.right_ml;
    if (split) {
      if (l.value != null) payload.left_ml = l.value;
      if (r.value != null) payload.right_ml = r.value;
    } else if (t.value != null) payload.total_ml = t.value;
    saving = true;
    try {
      if (action === "start") {
        const saved = await store.insert({
          type: "pump",
          started_at: new Date(),
          payload,
          note: note || null,
        });
        openSheet("pump", saved);
      } else if (editing) {
        const patch =
          action === "stop"
            ? { ...stopPatch(base!, new Date()), payload, note: note || null }
            : manual
              ? {
                  started_at: startedAt.toISOString(),
                  ended_at: new Date(
                    +startedAt + (m.value ?? 0) * 60000,
                  ).toISOString(),
                  payload,
                  note: note || null,
                }
              : // Timer sheet whose pump was stopped elsewhere: keep that stop time; the
                // hidden 20-minute default must never become the session's length.
                { payload, note: note || null };
        const saved = await store.update(editing.id, patch, {
          expectedUpdatedAt: base?.updated_at,
          undoLabel: action === "stop" ? "Pump stopped" : "Updated pump",
        });
        if (action === "stop") openSheet("pump", saved);
        else closeSheet();
      } else {
        await store.insert(
          {
            type: "pump",
            started_at: startedAt,
            ended_at: new Date(+startedAt + (m.value ?? 0) * 60000),
            payload,
            note: note || null,
          },
          { undoLabel: "Pump logged" },
        );
        closeSheet();
      }
    } catch (e) {
      if (e instanceof ConflictError && e.latest) conflict = e.latest;
      else err = "Could not save. Check whether a pump is already running.";
    } finally {
      saving = false;
    }
  }
</script>

<Sheet
  title="Pump"
  color="var(--pump)"
  dark
  onsave={manual ? () => save() : undefined}
  {saving}
  {dirty}
>
  {#if conflict}<ConflictBar
      latest={conflict}
      onUseTheirs={() => openSheet("pump", conflict!)}
      onKeepMine={() => {
        base = conflict!;
        conflict = null;
        // Re-save the user's edits on top of the newer version (the same contract as the other sheets).
        save(running && !base?.ended_at ? "stop" : "save");
      }}
    />{/if}
  {#if running}<p class="clock">
      Pumping · {fmtDuration(sleepElapsedS(editing!, store.now))}
    </p>{/if}
  {#if manual}<TimeRow bind:value={startedAt} /><label class="row"
      ><span>Duration (min)</span><input
        type="number"
        min="0"
        bind:value={minutes}
      /></label
    >{/if}
  <label class="row"
    ><span>Separate left / right amounts</span><input
      type="checkbox"
      bind:checked={split}
    /></label
  >
  {#if split}<label class="row"
      ><span>Left (mL)</span><input
        type="number"
        min="0"
        bind:value={left}
      /></label
    ><label class="row"
      ><span>Right (mL)</span><input
        type="number"
        min="0"
        bind:value={right}
      /></label
    >
  {:else}<label class="row"
      ><span>Total (mL)</span><input
        type="number"
        min="0"
        bind:value={total}
      /></label
    >{/if}
  <NoteRow bind:value={note} />{#if err}<p class="err" role="alert">
      {err}
    </p>{/if}
  {#snippet footer()}
    {#if running}<button
        class="btn-primary"
        disabled={saving}
        onclick={() => save("stop")}>Stop pump</button
      >
    {:else if !manual}<button
        class="btn-primary"
        disabled={saving}
        onclick={() => save("start")}>Start pump</button
      ><button class="btn-link" onclick={() => (manual = true)}
        >Log earlier</button
      >{/if}
    {#if editing}<button
        class="btn-link danger"
        disabled={saving}
        onclick={async () => {
          await store.remove(editing.id, "Pump deleted", base?.updated_at);
          closeSheet();
        }}>Delete</button
      >{/if}
  {/snippet}
</Sheet>

<style>
  input[type="number"] {
    width: 90px;
    background: var(--card-2);
    color: var(--text);
    padding: 8px;
    border: 0;
    border-radius: 8px;
    font-size: 20px;
  }
  .clock {
    padding: 20px;
    font-size: 26px;
  }
  .err {
    padding: 12px 20px;
    color: var(--danger);
  }
</style>
