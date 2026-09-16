import type { RealtimeChannel, Session } from '@supabase/supabase-js';
import { supabase } from '$lib/supabase';
import type { Caregiver, Child, Entry, EntryType, Payload, Prefs } from './types';
import { toast } from './toast.svelte';

const LOAD_DAYS = 60;

class Store {
  session = $state<Session | null>(null);
  ready = $state(false); // auth resolved
  loaded = $state(false); // household + entries loaded
  error = $state<string | null>(null);

  caregiver = $state<Caregiver | null>(null);
  caregivers = $state<Caregiver[]>([]);
  child = $state<Child | null>(null);
  prefs = $state<Prefs>({});
  entries = $state<Entry[]>([]);

  /** Ticks once a minute so "x ago" labels and running timers refresh. */
  now = $state(new Date());

  private channel: RealtimeChannel | null = null;
  private ticker: ReturnType<typeof setInterval> | null = null;

  async init() {
    const sb = supabase();
    const { data } = await sb.auth.getSession();
    this.session = data.session;
    this.ready = true;
    sb.auth.onAuthStateChange((_evt, session) => {
      const was = this.session?.user.id;
      this.session = session;
      if (session && session.user.id !== was) this.loadHousehold();
      if (!session) this.teardown();
    });
    if (this.session) await this.loadHousehold();
    if (!this.ticker) this.ticker = setInterval(() => (this.now = new Date()), 15_000);
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible' && this.session) {
        this.now = new Date();
        this.refreshEntries();
      }
    });
  }

  async loadHousehold() {
    const sb = supabase();
    this.error = null;
    const uid = this.session!.user.id;
    const [{ data: cgs, error: e1 }, { data: kids, error: e2 }, { data: prefs }] = await Promise.all([
      sb.from('caregivers').select('*'),
      sb.from('children').select('*'),
      sb.from('household_prefs').select('prefs').maybeSingle()
    ]);
    if (e1 || e2) {
      this.error = (e1 ?? e2)!.message;
      return;
    }
    this.caregivers = cgs ?? [];
    this.caregiver = this.caregivers.find((c) => c.user_id === uid) ?? null;
    this.child = kids?.[0] ?? null;
    this.prefs = (prefs?.prefs as Prefs) ?? {};
    if (!this.caregiver) {
      this.error = 'not-in-household';
      return;
    }
    await this.refreshEntries();
    this.subscribe();
    this.loaded = true;
  }

  async refreshEntries() {
    const sb = supabase();
    const since = new Date(Date.now() - LOAD_DAYS * 86400_000).toISOString();
    const { data, error } = await sb
      .from('entries')
      .select('*')
      .is('deleted_at', null)
      .gte('started_at', since)
      .order('started_at', { ascending: false })
      .limit(5000);
    if (error) {
      this.error = error.message;
      return;
    }
    // Include any running timer regardless of age.
    const { data: running } = await sb.from('entries').select('*').is('deleted_at', null).is('ended_at', null);
    const map = new Map<string, Entry>();
    for (const e of [...(data ?? []), ...(running ?? [])]) map.set(e.id, e as Entry);
    this.entries = [...map.values()];
  }

  /** Older entries for History; merged into the same list. */
  async loadOlder(beforeIso: string, days = 30): Promise<number> {
    const sb = supabase();
    const from = new Date(new Date(beforeIso).getTime() - days * 86400_000).toISOString();
    const { data } = await sb
      .from('entries')
      .select('*')
      .is('deleted_at', null)
      .lt('started_at', beforeIso)
      .gte('started_at', from)
      .order('started_at', { ascending: false });
    if (data?.length) {
      const ids = new Set(this.entries.map((e) => e.id));
      this.entries = [...this.entries, ...(data as Entry[]).filter((e) => !ids.has(e.id))];
    }
    return data?.length ?? 0;
  }

  private subscribe() {
    const sb = supabase();
    if (this.channel) sb.removeChannel(this.channel);
    this.channel = sb
      .channel('entries')
      .on('postgres_changes', { event: '*', schema: 'public', table: 'entries', filter: `household_id=eq.${this.caregiver!.household_id}` }, (p) => {
        if (p.eventType === 'DELETE') {
          const id = (p.old as { id: string }).id;
          this.entries = this.entries.filter((e) => e.id !== id);
          return;
        }
        const row = p.new as Entry;
        this.upsertLocal(row);
      })
      .on('postgres_changes', { event: '*', schema: 'public', table: 'household_prefs' }, (p) => {
        if (p.new && 'prefs' in p.new) this.prefs = (p.new as { prefs: Prefs }).prefs;
      })
      .subscribe();
  }

  private teardown() {
    if (this.channel) supabase().removeChannel(this.channel);
    this.channel = null;
    this.entries = [];
    this.caregiver = null;
    this.child = null;
    this.loaded = false;
  }

  private upsertLocal(row: Entry) {
    if (row.deleted_at) {
      this.entries = this.entries.filter((e) => e.id !== row.id);
      return;
    }
    const i = this.entries.findIndex((e) => e.id === row.id);
    if (i === -1) this.entries = [row, ...this.entries];
    else {
      const next = this.entries.slice();
      next[i] = row;
      this.entries = next;
    }
  }

  // ---- writes -------------------------------------------------------------

  async insert(input: { type: EntryType; started_at: Date; ended_at?: Date | null; payload: Payload; note?: string | null }, opts: { undoLabel?: string } = {}) {
    const sb = supabase();
    const uid = this.session!.user.id;
    const row = {
      household_id: this.caregiver!.household_id,
      child_id: input.type === 'pump' ? null : this.child!.id,
      type: input.type,
      started_at: input.started_at.toISOString(),
      ended_at: input.ended_at ? input.ended_at.toISOString() : null,
      payload: input.payload,
      note: input.note ?? null,
      created_by: uid,
      updated_by: uid
    };
    const { data, error } = await sb.from('entries').insert(row).select().single();
    if (error) {
      toast(`Couldn't save: ${error.message}`);
      throw error;
    }
    const saved = data as Entry;
    this.upsertLocal(saved);
    if (opts.undoLabel !== undefined) {
      toast(opts.undoLabel, { undo: () => this.hardDelete(saved.id) });
    }
    return saved;
  }

  async update(id: string, patch: Partial<Pick<Entry, 'started_at' | 'ended_at' | 'payload' | 'note'>>, opts: { undoLabel?: string } = {}) {
    const sb = supabase();
    const before = this.entries.find((e) => e.id === id);
    const { data, error } = await sb
      .from('entries')
      .update({ ...patch, updated_by: this.session!.user.id })
      .eq('id', id)
      .select()
      .single();
    if (error) {
      toast(`Couldn't save: ${error.message}`);
      throw error;
    }
    this.upsertLocal(data as Entry);
    if (opts.undoLabel !== undefined && before) {
      const { started_at, ended_at, payload, note } = before;
      toast(opts.undoLabel, {
        undo: async () => {
          await this.update(id, { started_at, ended_at, payload, note });
        }
      });
    }
    return data as Entry;
  }

  /** Soft delete with undo. */
  async remove(id: string, label = 'Deleted') {
    const sb = supabase();
    const { error } = await sb.from('entries').update({ deleted_at: new Date().toISOString(), updated_by: this.session!.user.id }).eq('id', id);
    if (error) {
      toast(`Couldn't delete: ${error.message}`);
      throw error;
    }
    this.entries = this.entries.filter((e) => e.id !== id);
    toast(label, {
      undo: async () => {
        const { data } = await sb.from('entries').update({ deleted_at: null }).eq('id', id).select().single();
        if (data) this.upsertLocal(data as Entry);
      }
    });
  }

  /** Used by undo of a fresh insert: nothing to keep. */
  private async hardDelete(id: string) {
    const sb = supabase();
    this.entries = this.entries.filter((e) => e.id !== id);
    await sb.from('entries').delete().eq('id', id);
  }

  async savePrefs(patch: Partial<Prefs>) {
    const sb = supabase();
    const next = { ...this.prefs, ...patch };
    this.prefs = next;
    await sb.from('household_prefs').upsert({ household_id: this.caregiver!.household_id, prefs: next });
  }

  async signOut() {
    await supabase().auth.signOut();
  }

  initial(userId: string | null): string {
    const c = this.caregivers.find((c) => c.user_id === userId);
    return c ? c.display_name[0] : '';
  }
}

export const store = new Store();
