import type { PostgrestFilterBuilder } from '@supabase/postgrest-js';
import type { RealtimeChannel, Session } from '@supabase/supabase-js';
import { supabase } from '$lib/supabase';
import type { Caregiver, Child, Entry, EntryType, Payload, Prefs } from './types';
import { toast } from './toast.svelte';

const INITIAL_DAYS = 60;
const PAGE = 1000; // PostgREST's default max-rows; page below it so nothing is silently truncated
const DAY = 86400_000;

export class ConflictError extends Error {
  constructor() {
    super('Changed on another phone');
    this.name = 'ConflictError';
  }
}

type Patch = Partial<Pick<Entry, 'started_at' | 'ended_at' | 'payload' | 'note' | 'deleted_at'>>;

class Store {
  session = $state<Session | null>(null);
  ready = $state(false); // auth resolved
  loaded = $state(false); // household + entries loaded once
  /** Fatal: household could not be loaded. Shown instead of the app. */
  error = $state<string | null>(null);
  /** Non-fatal: the last refresh failed. Shown as a banner over the still-usable app. */
  syncError = $state<string | null>(null);
  /** Realtime channel state for the header indicator. */
  realtime = $state<'connecting' | 'live' | 'offline'>('connecting');

  caregiver = $state<Caregiver | null>(null);
  caregivers = $state<Caregiver[]>([]);
  child = $state<Child | null>(null);
  prefs = $state<Prefs>({});
  entries = $state<Entry[]>([]);
  /** Every entry with started_at >= loadedSince is in memory (plus running timers). */
  loadedSince = $state<string>(new Date(Date.now() - INITIAL_DAYS * DAY).toISOString());
  /** True once loadedSince is before the child's birth: nothing older exists. */
  exhausted = $state(false);

  /** Ticks so "x ago" labels refresh. */
  now = $state(new Date());

  private channel: RealtimeChannel | null = null;
  private ticker: ReturnType<typeof setInterval> | null = null;
  private everSubscribed = false;

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
      if (document.visibilityState === 'visible' && this.session && this.loaded) {
        this.now = new Date();
        this.refreshEntries();
        this.refreshPrefs();
      }
    });
    window.addEventListener('online', () => {
      if (this.loaded) this.refreshEntries();
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
    // Subscribe first so nothing that changes during the initial fetch is missed.
    this.subscribe();
    const ok = await this.refreshEntries();
    if (!ok && !this.loaded) {
      this.error = this.syncError ?? 'Could not load entries';
      return;
    }
    this.loaded = true;
  }

  /** Runs a query in pages so the server's max-rows cap never truncates a result. */
  private async fetchAll(build: () => PostgrestFilterBuilder<any, any, any, any>): Promise<{ rows: Entry[]; error: string | null }> {
    const rows: Entry[] = [];
    for (let offset = 0; ; offset += PAGE) {
      const { data, error } = await build().order('started_at', { ascending: false }).order('id', { ascending: false }).range(offset, offset + PAGE - 1);
      if (error) return { rows, error: error.message };
      rows.push(...((data ?? []) as Entry[]));
      if (!data || data.length < PAGE) return { rows, error: null };
    }
  }

  /** Reloads the already-loaded window [loadedSince, now] plus any running timer. Older pages stay. */
  async refreshEntries(): Promise<boolean> {
    const sb = supabase();
    const since = this.loadedSince;
    const [win, running] = await Promise.all([
      this.fetchAll(() => sb.from('entries').select('*').is('deleted_at', null).gte('started_at', since)),
      this.fetchAll(() => sb.from('entries').select('*').is('deleted_at', null).is('ended_at', null).in('type', ['breastfeed', 'pump']))
    ]);
    if (win.error || running.error) {
      this.syncError = win.error ?? running.error;
      return false;
    }
    const fresh = new Map<string, Entry>();
    for (const e of [...win.rows, ...running.rows]) fresh.set(e.id, e);
    // Keep older pages that were loaded via loadOlder; replace everything in the refreshed window.
    const older = this.entries.filter((e) => e.started_at < since && !fresh.has(e.id) && !(e.ended_at === null && (e.type === 'breastfeed' || e.type === 'pump')));
    this.entries = [...fresh.values(), ...older];
    this.syncError = null;
    return true;
  }

  async refreshPrefs() {
    const { data } = await supabase().from('household_prefs').select('prefs').maybeSingle();
    if (data?.prefs) this.prefs = data.prefs as Prefs;
  }

  /** Loads the next `days` before loadedSince into memory. Returns rows added, or null on error. */
  async loadOlder(days = 30): Promise<number | null> {
    if (this.exhausted) return 0;
    const sb = supabase();
    const to = this.loadedSince;
    const from = new Date(new Date(to).getTime() - days * DAY).toISOString();
    const { rows, error } = await this.fetchAll(() => sb.from('entries').select('*').is('deleted_at', null).gte('started_at', from).lt('started_at', to));
    if (error) {
      toast(`Couldn't load older entries: ${error}`);
      return null;
    }
    const ids = new Set(this.entries.map((e) => e.id));
    this.entries = [...this.entries, ...rows.filter((e) => !ids.has(e.id))];
    this.loadedSince = from;
    const birth = this.child ? new Date(this.child.birth_date).getTime() - 7 * DAY : 0;
    if (new Date(from).getTime() <= birth) this.exhausted = true;
    return rows.length;
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
        this.upsertLocal(p.new as Entry);
      })
      .on('postgres_changes', { event: '*', schema: 'public', table: 'household_prefs' }, (p) => {
        if (p.new && 'prefs' in p.new) this.prefs = (p.new as { prefs: Prefs }).prefs;
      })
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') {
          this.realtime = 'live';
          // After a dropped connection, catch up on anything missed while offline.
          if (this.everSubscribed && this.loaded) this.refreshEntries();
          this.everSubscribed = true;
        } else if (status === 'CHANNEL_ERROR' || status === 'TIMED_OUT' || status === 'CLOSED') {
          this.realtime = 'offline';
        }
      });
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
      // Undo of a fresh insert is a soft delete too: recoverable, and safe if the other phone already touched it.
      toast(opts.undoLabel, {
        undo: async () => {
          await this.softDelete(saved.id, saved.updated_at);
        }
      });
    }
    return saved;
  }

  /**
   * Conditional update: only applies if the row's updated_at still equals `expectedUpdatedAt`.
   * On a mismatch the local copy is refreshed and a ConflictError is thrown so the sheet can say so.
   */
  async update(id: string, patch: Patch, opts: { undoLabel?: string; expectedUpdatedAt?: string } = {}) {
    const sb = supabase();
    const before = this.entries.find((e) => e.id === id);
    const expected = opts.expectedUpdatedAt ?? before?.updated_at;
    let q = sb.from('entries').update({ ...patch, updated_by: this.session!.user.id }).eq('id', id);
    if (expected) q = q.eq('updated_at', expected);
    const { data, error } = await q.select();
    if (error) {
      toast(`Couldn't save: ${error.message}`);
      throw error;
    }
    if (!data || data.length === 0) {
      await this.reloadOne(id);
      toast('Changed on the other phone. Reopen to see the latest.');
      throw new ConflictError();
    }
    const after = data[0] as Entry;
    this.upsertLocal(after);
    if (opts.undoLabel !== undefined && before) {
      const { started_at, ended_at, payload, note } = before;
      toast(opts.undoLabel, {
        undo: async () => {
          try {
            await this.update(id, { started_at, ended_at, payload, note }, { expectedUpdatedAt: after.updated_at });
          } catch {
            /* reported by update() */
          }
        }
      });
    }
    return after;
  }

  private async reloadOne(id: string) {
    const { data } = await supabase().from('entries').select('*').eq('id', id).maybeSingle();
    if (data) this.upsertLocal(data as Entry);
    else this.entries = this.entries.filter((e) => e.id !== id);
  }

  /** Soft delete with an undo toast. */
  async remove(id: string, label = 'Deleted', expectedUpdatedAt?: string) {
    const deleted = await this.softDelete(id, expectedUpdatedAt);
    if (!deleted) return;
    toast(label, {
      undo: async () => {
        try {
          await this.update(id, { deleted_at: null }, { expectedUpdatedAt: deleted.updated_at });
        } catch {
          /* reported by update() */
        }
      }
    });
  }

  private async softDelete(id: string, expectedUpdatedAt?: string): Promise<Entry | null> {
    try {
      return await this.update(id, { deleted_at: new Date().toISOString() }, { expectedUpdatedAt });
    } catch {
      return null; // toast already shown
    }
  }

  async savePrefs(patch: Partial<Prefs>) {
    const sb = supabase();
    const prev = this.prefs;
    const next = { ...this.prefs, ...patch };
    this.prefs = next;
    const { error } = await sb.from('household_prefs').upsert({ household_id: this.caregiver!.household_id, prefs: next });
    if (error) {
      this.prefs = prev;
      toast(`Couldn't save preference: ${error.message}`);
    }
  }

  /** Every entry for the household, oldest first, for export. */
  async fetchEverything(): Promise<Entry[] | null> {
    const sb = supabase();
    const { rows, error } = await this.fetchAll(() => sb.from('entries').select('*'));
    if (error) {
      toast(`Export failed: ${error}`);
      return null;
    }
    return rows.sort((a, b) => a.started_at.localeCompare(b.started_at));
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
