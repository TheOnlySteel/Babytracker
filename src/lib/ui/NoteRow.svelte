<script lang="ts">
  let { value = $bindable('') }: { value: string } = $props();
</script>

<label class="row note-row">
  <span class="row-label">Notes</span>
  <input type="text" placeholder="" bind:value autocapitalize="sentences" />
</label>

<style>
  .note-row { gap: 16px; }
  input {
    flex: 1; min-width: 0; text-align: right; background: none; border: 0; outline: none; font-size: 17px; padding: 8px 0;
  }
</style>
