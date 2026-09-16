<script lang="ts">
  import { store } from '$lib/data/store.svelte';
</script>

<main>
  <h2>Account</h2>
  <div class="card">
    <div class="row"><span class="row-label">Signed in as</span><span class="row-value">{store.caregiver?.display_name}</span></div>
    <div class="row"><span class="row-label">Email</span><span class="row-value muted">{store.session?.user.email}</span></div>
    <div class="row"><span class="row-label">Household</span><span class="row-value muted">{store.caregivers.map((c) => c.display_name).join(', ')}</span></div>
    <div class="row"><span class="row-label">Child</span><span class="row-value">{store.child?.name} · born {store.child?.birth_date}</span></div>
  </div>
  <div class="card">
    <div class="row col">
      <span class="row-label">Install</span>
      <span class="muted">On iPhone: Share → Add to Home Screen. The app then opens full-screen and stays signed in.</span>
    </div>
  </div>
  <button class="btn-ghost" onclick={() => store.signOut()}>Sign out</button>
</main>

<style>
  main { padding: calc(var(--safe-t) + 24px) 16px calc(var(--tab-h) + var(--safe-b) + 24px); display: flex; flex-direction: column; gap: 16px; }
  h2 { font-size: 30px; padding: 0 4px; }
  .card { background: var(--card); border-radius: var(--radius); overflow: hidden; }
  .card .row:last-child { border-bottom: 0; }
  .col { flex-direction: column; align-items: flex-start; gap: 6px; }
</style>
