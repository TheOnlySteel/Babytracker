<script lang="ts">
  import Header from '$lib/components/Header.svelte';
  import Card from '$lib/components/Card.svelte';
</script>

<Header />
<main>
  <Card card="feed" title="Feed" color="var(--feed)" lastLabel="Last feeding" sheetFor="feed-picker" sameAgain />
  <Card card="diaper" title="Diaper" color="var(--diaper)" lastLabel="Last change" sheetFor="diaper" sameAgain />
  <Card card="pump" title="Pump" color="var(--pump)" lastLabel="Last session" sheetFor="pump" />
  <Card card="sleep" title="Sleep" color="var(--sleep)" lastLabel="Last sleep" sheetFor="sleep" />
</main>

<style>
  main { padding-bottom: calc(var(--tab-h) + var(--safe-b) + 24px); }
</style>
