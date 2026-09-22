<script lang="ts">
  import { supabase } from '$lib/supabase';
  let email = $state('');
  let password = $state('');
  let busy = $state(false);
  let error = $state('');

  async function signIn() {
    busy = true;
    error = '';
    const { error: e } = await supabase().auth.signInWithPassword({ email: email.trim(), password });
    busy = false;
    if (e) error = e.message === 'Invalid login credentials' ? 'Wrong email or password.' : e.message;
  }
</script>

<main class="signin">
  <h1>Cradlewatch</h1>
  <p class="muted">Sign in once on this phone; it stays signed in.</p>
  <form
    onsubmit={(e) => {
      e.preventDefault();
      signIn();
    }}
  >
    <label>Email<input type="email" bind:value={email} autocomplete="username" required /></label>
    <label>Password<input type="password" bind:value={password} autocomplete="current-password" required /></label>
    <button class="btn-primary" disabled={busy || !email || !password}>{busy ? 'Signing in…' : 'Sign in'}</button>
  </form>
  {#if error}<p class="err">{error}</p>{/if}
  <p class="muted small">Forgot the password? The other caregiver can reset it from the Supabase dashboard (Authentication → Users → Reset password); no email is sent by this app.</p>
</main>

<style>
  .signin { padding: calc(var(--safe-t) + 80px) 24px 24px; max-width: 420px; margin: 0 auto; }
  h1 { font-size: 48px; margin-bottom: 8px; }
  form { display: flex; flex-direction: column; gap: 12px; margin-top: 24px; }
  label { display: flex; flex-direction: column; gap: 6px; font-size: 15px; color: var(--muted); }
  input { width: 100%; min-height: 52px; padding: 0 16px; border-radius: 12px; border: 1px solid var(--rule); background: var(--card); font-size: 18px; outline: none; }
  input:focus { border-color: var(--accent); }
  .err { color: var(--danger); }
  .small { font-size: 14px; margin-top: 24px; }
</style>
