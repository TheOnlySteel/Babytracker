<script lang="ts">
  import { ui } from '$lib/data/ui.svelte';
  import FeedPicker from './sheets/FeedPicker.svelte';
  import BottleSheet from './sheets/BottleSheet.svelte';
  import DiaperSheet from './sheets/DiaperSheet.svelte';
  import BreastfeedSheet from './sheets/BreastfeedSheet.svelte';
  import PumpSheet from './sheets/PumpSheet.svelte';
  import SummarySheet from './sheets/SummarySheet.svelte';
</script>

{#if ui.sheet}
  {#key ui.sheet}
    {#if ui.sheet.kind === 'feed-picker'}<FeedPicker />
    {:else if ui.sheet.kind === 'bottle'}<BottleSheet entry={ui.sheet.entry} />
    {:else if ui.sheet.kind === 'diaper'}<DiaperSheet entry={ui.sheet.entry} />
    {:else if ui.sheet.kind === 'breastfeed'}<BreastfeedSheet entry={ui.sheet.entry} />
    {:else if ui.sheet.kind === 'pump'}<PumpSheet entry={ui.sheet.entry} />
    {:else if ui.sheet.kind === 'summary'}<SummarySheet />
    {/if}
  {/key}
{/if}
