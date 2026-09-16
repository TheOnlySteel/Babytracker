<script lang="ts">
  import Header from '$lib/components/Header.svelte';
  import SummaryView from '$lib/components/SummaryView.svelte';
</script>

<Header />
<main>
  <h2>Summary</h2>
  <SummaryView />
</main>

<style>
  main { padding-bottom: calc(var(--tab-h) + var(--safe-b) + 24px); }
  h2 { font-size: 30px; padding: 8px 20px 0; }
</style>
