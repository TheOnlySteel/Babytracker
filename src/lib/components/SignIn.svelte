<script lang="ts">
  import { supabase } from '$lib/supabase';
  let email = $state('');
  let code = $state('');
  let stage = $state<'email' | 'code'>('email');
  let busy = $state(false);
  let error = $state('');

  async function sendCode() {
    busy = true; error = '';
    const { error: e } = await supabase().auth.signInWithOtp({ email: email.trim(), options: { shouldCreateUser: true } });
    busy = false;
    if (e) { error = e.message; return; }
    stage = 'code';
  }
  async function verify() {
    busy = true; error = '';
    const { error: e } = await supabase().auth.verifyOtp({ email: email.trim(), token: code.trim(), type: 'email' });
    busy = false;
    if (e) error = e.message;
  }
</script>

<main class="signin">
  <h1>Rosalie</h1>
  <p class="muted">Sign in with your email. You'll get a 6-digit code.</p>
  {#if stage === 'email'}
    <form onsubmit={(e) => { e.preventDefault(); sendCode(); }}>
      <input type="email" bind:value={email} placeholder="you@example.com" autocomplete="email" required />
      <button class="btn-primary" disabled={busy || !email}>Send code</button>
    </form>
  {:else}
    <form onsubmit={(e) => { e.preventDefault(); verify(); }}>
      <p>Code sent to <strong>{email}</strong></p>
      <input type="text" inputmode="numeric" autocomplete="one-time-code" bind:value={code} placeholder="123456" maxlength="8" required />
      <button class="btn-primary" disabled={busy || code.length < 6}>Sign in</button>
      <button type="button" class="btn-ghost" onclick={() => (stage = 'email')}>Use a different email</button>
    </form>
  {/if}
  {#if error}<p class="err">{error}</p>{/if}
</main>

<style>
  .signin { padding: calc(var(--safe-t) + 80px) 24px 24px; max-width: 420px; margin: 0 auto; }
  h1 { font-size: 48px; margin-bottom: 8px; }
  form { display: flex; flex-direction: column; gap: 12px; margin-top: 24px; }
  input { min-height: 52px; padding: 0 16px; border-radius: 12px; border: 1px solid var(--rule); background: var(--card); font-size: 18px; outline: none; }
  input:focus { border-color: var(--accent); }
  .err { color: var(--danger); }
</style>
