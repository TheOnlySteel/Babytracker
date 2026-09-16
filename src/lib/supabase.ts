import { createClient, type SupabaseClient } from '@supabase/supabase-js';
import { env } from '$env/dynamic/public';

export const SUPABASE_URL = env.PUBLIC_SUPABASE_URL ?? '';
export const SUPABASE_ANON_KEY = env.PUBLIC_SUPABASE_ANON_KEY ?? '';
export const configured = Boolean(SUPABASE_URL && SUPABASE_ANON_KEY);

let client: SupabaseClient | null = null;
export function supabase(): SupabaseClient {
  if (!client) {
    client = createClient(SUPABASE_URL, SUPABASE_ANON_KEY, {
      auth: { persistSession: true, autoRefreshToken: true, detectSessionInUrl: true },
      realtime: { params: { eventsPerSecond: 10 } }
    });
  }
  return client;
}
