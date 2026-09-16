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
  <h1>Rosalie</h1>
  <p class="muted">Sign in once on this phone; it stays signed in.</p>
  <form
    onsubmit={(e) => {
      e.preventDefault();
      signIn();
    }}
  >
    <input type="email" bind:value={email} placeholder="Email" autocomplete="username" required />
    <input type="password" bind:value={password} placeholder="Password" autocomplete="current-password" required />
    <button class="btn-primary" disabled={busy || !email || !password}>{busy ? 'Signing in…' : 'Sign in'}</button>
  </form>
  {#if error}<p class="err">{error}</p>{/if}
  <p class="muted small">Forgot it? Reset it in the Supabase dashboard under Authentication → Users.</p>
</main>

<style>
  .signin { padding: calc(var(--safe-t) + 80px) 24px 24px; max-width: 420px; margin: 0 auto; }
  h1 { font-size: 48px; margin-bottom: 8px; }
  form { display: flex; flex-direction: column; gap: 12px; margin-top: 24px; }
  input { min-height: 52px; padding: 0 16px; border-radius: 12px; border: 1px solid var(--rule); background: var(--card); font-size: 18px; outline: none; }
  input:focus { border-color: var(--accent); }
  .err { color: var(--danger); }
  .small { font-size: 14px; margin-top: 24px; }
</style>
