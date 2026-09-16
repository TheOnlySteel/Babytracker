<script lang="ts">
  import { fmtWhen } from '$lib/data/format';
  let { label = 'Start Time', value = $bindable() }: { label?: string; value: Date } = $props();

  // datetime-local wants local "YYYY-MM-DDTHH:MM"
  function toLocal(d: Date): string {
    const p = (n: number) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}`;
  }
  function onchange(e: Event) {
    const v = (e.target as HTMLInputElement).value;
    if (v) value = new Date(v);
  }
</script>

<label class="row time-row">
  <span class="row-label">{label}</span>
  <span class="row-value">{fmtWhen(value)}</span>
  <input type="datetime-local" value={toLocal(value)} {onchange} aria-label={label} />
</label>

<style>
  .time-row { position: relative; cursor: pointer; }
  input {
    position: absolute; inset: 0; opacity: 0; width: 100%; height: 100%;
    /* iOS needs a real size to open the picker */
    font-size: 16px;
  }
</style>
