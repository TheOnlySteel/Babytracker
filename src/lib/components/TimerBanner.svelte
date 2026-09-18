<script lang="ts">
  import { onMount } from "svelte";
  import { store, ConflictError } from "$lib/data/store.svelte";
  import type { Entry, BreastfeedPayload, SleepPayload } from "$lib/data/types";
  import { toastError } from "$lib/data/toast.svelte";
  import { openEntry, ui } from "$lib/data/ui.svelte";
  import { runningElapsed, sleepElapsedS, entryLabel } from "$lib/data/derive";
  import { stopPatch } from "$lib/data/timers";
  import { fmtDuration } from "$lib/data/format";
  const running = $derived(
    store.entries.filter(
      (e) =>
        !e.deleted_at &&
        !e.ended_at &&
        ["breastfeed", "pump", "sleep"].includes(e.type),
    ),
  );
  let tick = $state(new Date()),
    stopping = $state<string | null>(null);
  onMount(() => {
    const t = setInterval(() => (tick = new Date()), 1000);
    return () => clearInterval(t);
  });
  function elapsed(e: Entry) {
    if (e.type !== "breastfeed") return sleepElapsedS(e, tick);
    const p = runningElapsed(e.payload as BreastfeedPayload, tick);
    return p.left_s + p.right_s;
  }
  /** "Breastfeeding · left", "Breastfeeding · paused", "Pumping", "Napping · crib · auto". */
  function label(e: Entry) {
    if (e.type === "breastfeed") {
      const open = runningElapsed(e.payload as BreastfeedPayload, tick).open;
      return `Breastfeeding · ${open ?? "paused"}`;
    }
    if (e.type === "pump") return "Pumping";
    return entryLabel(e).replace(/^Nap\b/, "Napping").replace(/^Night sleep\b/, "Sleeping");
  }
  const paused = (e: Entry) =>
    e.type === "breastfeed" && !runningElapsed(e.payload as BreastfeedPayload, tick).open;
  function undoLabel(e: Entry, seconds: number) {
    if (e.type === "breastfeed") return `Logged ${fmtDuration(seconds)} breastfeed`;
    if (e.type === "pump") return `Logged ${fmtDuration(seconds)} pump`;
    return (e.payload as SleepPayload).source === "cradlewise" ? "Crib nap ended" : `Logged ${fmtDuration(seconds)} nap`;
  }
  /** Spec §6: the banner can stop the timer directly, from any screen, without opening the sheet. */
  async function stop(e: Entry) {
    if (stopping) return;
    stopping = e.id;
    try {
      await store.update(e.id, stopPatch(e, new Date()), {
        expectedUpdatedAt: e.updated_at,
        undoLabel: undoLabel(e, elapsed(e)),
      });
    } catch (err) {
      if (err instanceof ConflictError)
        toastError(
          "The timer changed on the other phone; showing the latest.",
        );
    } finally {
      stopping = null;
    }
  }
</script>

{#each running as e (e.id)}
  {#if ui.sheet?.kind !== e.type}
    <div
      class="banner"
      style:background={e.type === "sleep"
        ? "var(--sleep)"
        : e.type === "pump"
          ? "var(--pump)"
          : "var(--feed)"}
    >
      <button class="open" onclick={() => openEntry(e)}
        ><span class="dot" class:paused={paused(e)}></span><span
          >{label(e)}{#if store.realtime === "offline"}<span class="rt"> · reconnecting</span>{/if}</span
        ><b>{fmtDuration(elapsed(e))}</b></button
      >
      <button class="stop" disabled={stopping !== null} onclick={() => stop(e)}
        >{e.type === "sleep" &&
        (e.payload as SleepPayload).source === "cradlewise"
          ? "End"
          : "Stop"}</button
      >
    </div>
  {/if}
{/each}

<style>
  .banner {
    display: flex;
    gap: 8px;
    padding: 6px 12px;
    color: var(--ink);
    font-weight: 600;
  }
  .open {
    flex: 1;
    display: flex;
    align-items: center;
    gap: 10px;
    text-align: left;
    min-height: 44px;
  }
  .open span:nth-child(2) {
    flex: 1;
  }
  .dot {
    width: 9px;
    height: 9px;
    border-radius: 50%;
    background: var(--danger);
    flex: none;
    animation: pulse 1.2s infinite;
  }
  .dot.paused { background: var(--ink); animation: none; opacity: 0.5; }
  .rt { font-weight: 400; opacity: 0.8; }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
  b {
    white-space: nowrap;
  }
  .stop {
    border: 1px solid;
    border-radius: 22px;
    min-width: 64px;
    min-height: 44px;
  }
  .stop:disabled {
    opacity: 0.5;
  }
</style>
