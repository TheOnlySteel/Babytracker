<script lang="ts">
  import { onMount } from "svelte";
  import { store } from "$lib/data/store.svelte";
  import type { Device } from "$lib/data/types";
  import CribStatus from "./CribStatus.svelte";
  let timezone = $state(store.timezone ?? '');
  $effect(() => {
    if (store.timezone) timezone = store.timezone;
  });
  let devices = $state<Device[]>([]),
    name = $state("Nursery pad"),
    key = $state(""),
    error = $state(""),
    busy = $state(false);
  async function run(action: () => Promise<unknown>) {
    busy = true;
    error = "";
    try {
      await action();
    } catch (e) {
      error = (e as Error).message ?? "Request failed";
    } finally {
      busy = false;
    }
  }
  onMount(() => {
    run(async () => {
      await store.refreshSleepStatus();
      devices = await store.listDevices();
    });
  });
  async function pair() {
    const result = await store.pairDevice(name);
    key = result.key;
    devices = await store.listDevices();
  }
  async function change(
    d: Device,
    mode: Device["boot_mode"] | null,
    revoke = false,
  ) {
    await store.manageDevice(d.id, mode, revoke);
    devices = await store.listDevices();
  }
</script>

<section>
  <h3>Sleep monitor</h3>
  <CribStatus />
  <label
    >Household timezone <input
      aria-label="Household timezone"
      bind:value={timezone}
    /></label
  ><button
    class="btn-ghost"
    disabled={busy}
    onclick={() => run(() => store.setTimezone(timezone))}>Save timezone</button
  >
  <p>
    {store.timezone} · {store.sleepStatus?.requests_24h ?? "—"} requests in the last
    24 hours
  </p>
  {#if store.sleepStatus?.source_error === "token_expired"}<p>
      The Cradlewise token expired. Update CW_TOKEN in the poller's Supabase
      secrets.
    </p>{/if}
  {#if store.sleepStatus?.history_error}<p>
      History needs review: {store.sleepStatus.history_error.replaceAll(
        "_",
        " ",
      )}. Live sleeps remain provisional.
    </p>{/if}
  <label
    ><input
      type="checkbox"
      checked={store.sleepStatus?.derive_enabled ?? false}
      disabled={busy || !store.sleepStatus}
      onchange={(e) => run(() => store.setDerivation(e.currentTarget.checked))}
    /> Automatically log crib sleep</label
  >
  <p class="muted">
    Enable after the standalone monitors have moved to NurseryPad and the live
    history format has been verified.
  </p>
</section>
<section>
  <h3>Devices</h3>
  {#each devices as d (d.id)}<div class="device">
      <strong>{d.name}</strong><span class="muted"
        >{d.revoked_at
          ? "Revoked"
          : d.last_seen_at
            ? `Last seen ${new Date(d.last_seen_at).toLocaleString()}`
            : "Never connected"}</span
      >
      {#if !d.revoked_at}<label
          >Boot mode <select
            value={d.boot_mode}
            disabled={busy}
            onchange={(e) =>
              run(() =>
                change(d, e.currentTarget.value as Device["boot_mode"]),
              )}
            ><option value="hub">Home hub</option><option value="dashboard"
              >Dashboard</option
            ><option value="lamp">Lamp</option></select
          ></label
        ><button
          class="btn-link danger"
          disabled={busy}
          onclick={() => run(() => change(d, null, true))}>Revoke</button
        >{/if}
    </div>{/each}
  <label>Device name <input maxlength="60" bind:value={name} /></label><button
    class="btn-ghost"
    disabled={busy || !name.trim()}
    onclick={() => run(pair)}>Pair device</button
  >
  {#if key}<p>
      Copy this key into the device's secrets.h. It is shown only here and
      cannot be retrieved later.
    </p>
    <code>{key}</code><button
      class="btn-ghost"
      onclick={() => run(() => navigator.clipboard.writeText(key))}
      >Copy key</button
    ><button class="btn-link" onclick={() => (key = "")}>Hide key</button>{/if}
</section>
{#if error}<p class="error" role="alert">{error}</p>{/if}

<style>
  section {
    background: var(--card);
    padding: 20px;
    border-radius: var(--radius);
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  h3 {
    font-size: 24px;
  }
  p {
    font-size: 14px;
    margin: 0;
  }
  label {
    display: flex;
    gap: 10px;
    align-items: center;
  }
  input:not([type="checkbox"]),
  select {
    background: var(--card-2);
    color: var(--text);
    padding: 8px;
    border: 1px solid var(--rule);
    border-radius: 8px;
    min-width: 0;
  }
  code {
    overflow-wrap: anywhere;
    user-select: all;
  }
  .device {
    display: flex;
    flex-direction: column;
    gap: 10px;
    border-bottom: 1px solid var(--rule);
    padding: 10px 0;
  }
  .error {
    color: var(--danger);
  }
</style>
