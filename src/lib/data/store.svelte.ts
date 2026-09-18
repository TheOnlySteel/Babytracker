import type { PostgrestFilterBuilder } from '@supabase/postgrest-js';
import type { RealtimeChannel, Session } from '@supabase/supabase-js';
import { supabase } from '$lib/supabase';
import type {
  Caregiver,
  Child,
  Entry,
  EntryType,
  Payload,
  Prefs,
  SleepStatus,
  Device,
} from './types';
import { toast, toastError } from './toast.svelte';

const INITIAL_DAYS = 60;
const PAGE = 1000; // PostgREST's default max-rows; page below it so nothing is silently truncated
const DAY = 86400_000;

export class ConflictError extends Error {
  /** The row as it is now, so the sheet can show what changed and let the caregiver decide. */
  latest: Entry | null;
  constructor(latest: Entry | null) {
    super('Changed on another phone');
    this.name = 'ConflictError';
    this.latest = latest;
  }
}

type Patch = Partial<
  Pick<Entry, 'started_at' | 'ended_at' | 'payload' | 'note' | 'deleted_at'>
>;

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
  sleepStatus = $state<SleepStatus | null>(null);
  /** Household IANA zone from monitor_settings; undefined until known, then the device zone is used. */
  timezone = $state<string | undefined>(undefined);
  /** Every entry with started_at >= loadedSince is in memory (plus running timers). */
  loadedSince = $state<string>(
    new Date(Date.now() - INITIAL_DAYS * DAY).toISOString(),
  );
  /** True once loadedSince is before the child's birth: nothing older exists. */
  exhausted = $state(false);

  /** Ticks so "x ago" labels refresh. */
  now = $state(new Date());

  private channel: RealtimeChannel | null = null;
  /** The monitor's own channel: an error here must never take the log's live sync down. */
  private sleepChannel: RealtimeChannel | null = null;
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
    if (!this.ticker)
      this.ticker = setInterval(() => (this.now = new Date()), 15_000);
    document.addEventListener('visibilitychange', () => {
      if (
        document.visibilityState === 'visible' &&
        this.session &&
        this.loaded
      ) {
        this.now = new Date();
        this.refreshEntries();
        this.refreshPrefs();
        this.refreshSleepStatus();
      }
    });
    window.addEventListener('online', () => {
      if (this.loaded) {
        this.refreshEntries();
        this.refreshSleepStatus();
      }
    });
  }

  async loadHousehold() {
    const sb = supabase();
    this.error = null;
    const uid = this.session!.user.id;
    const [
      { data: cgs, error: e1 },
      { data: kids, error: e2 },
      { data: prefs },
    ] = await Promise.all([
      sb.from('caregivers').select('*'),
      sb.from('children').select('*'),
      sb.from('household_prefs').select('prefs').maybeSingle(),
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
    // Additive: the log paints first; the monitor state arrives when the RPC answers.
    void this.refreshSleepStatus();
  }

  /** Runs a query in pages so the server's max-rows cap never truncates a result. */
  private async fetchAll(
    build: () => PostgrestFilterBuilder<any, any, any, any>,
  ): Promise<{ rows: Entry[]; error: string | null }> {
    const rows: Entry[] = [];
    for (let offset = 0; ; offset += PAGE) {
      const { data, error } = await build()
        .order('started_at', { ascending: false })
        .order('id', { ascending: false })
        .range(offset, offset + PAGE - 1);
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
      this.fetchAll(() =>
        sb
          .from('entries')
          .select('*')
          .is('deleted_at', null)
          .gte('started_at', since),
      ),
      this.fetchAll(() =>
        sb
          .from('entries')
          .select('*')
          .is('deleted_at', null)
          .is('ended_at', null)
          .in('type', ['breastfeed', 'pump', 'sleep']),
      ),
    ]);
    if (win.error || running.error) {
      this.syncError = win.error ?? running.error;
      return false;
    }
    const fresh = new Map<string, Entry>();
    for (const e of [...win.rows, ...running.rows]) fresh.set(e.id, e);
    // Keep older pages that were loaded via loadOlder; replace everything in the refreshed window.
    const older = this.entries.filter(
      (e) =>
        e.started_at < since &&
        !fresh.has(e.id) &&
        !(
          e.ended_at === null &&
          (e.type === 'breastfeed' || e.type === 'pump' || e.type === 'sleep')
        ),
    );
    this.entries = [...fresh.values(), ...older];
    this.syncError = null;
    return true;
  }

  async refreshPrefs() {
    const { data } = await supabase()
      .from('household_prefs')
      .select('prefs')
      .maybeSingle();
    if (data?.prefs) this.prefs = data.prefs as Prefs;
  }

  /** Loads the next `days` before loadedSince into memory. Returns rows added, or null on error. */
  async loadOlder(days = 30): Promise<number | null> {
    if (this.exhausted) return 0;
    const sb = supabase();
    const to = this.loadedSince;
    const from = new Date(new Date(to).getTime() - days * DAY).toISOString();
    const { rows, error } = await this.fetchAll(() =>
      sb
        .from('entries')
        .select('*')
        .is('deleted_at', null)
        .gte('started_at', from)
        .lt('started_at', to),
    );
    if (error) {
      toastError(`Couldn't load older entries: ${error}`);
      return null;
    }
    const ids = new Set(this.entries.map((e) => e.id));
    this.entries = [...this.entries, ...rows.filter((e) => !ids.has(e.id))];
    this.loadedSince = from;
    const birth = this.child
      ? new Date(this.child.birth_date).getTime() - 7 * DAY
      : 0;
    if (new Date(from).getTime() <= birth) this.exhausted = true;
    return rows.length;
  }

  private subscribe() {
    const sb = supabase();
    if (this.channel) sb.removeChannel(this.channel);
    this.channel = sb
      .channel('entries')
      .on(
        'postgres_changes',
        {
          event: '*',
          schema: 'public',
          table: 'entries',
          filter: `household_id=eq.${this.caregiver!.household_id}`,
        },
        (p) => {
          if (p.eventType === 'DELETE') {
            const id = (p.old as { id: string }).id;
            this.entries = this.entries.filter((e) => e.id !== id);
            return;
          }
          this.upsertLocal(p.new as Entry);
        },
      )
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'household_prefs' },
        (p) => {
          if (p.new && 'prefs' in p.new)
            this.prefs = (p.new as { prefs: Prefs }).prefs;
        },
      )
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') {
          this.realtime = 'live';
          // After a dropped connection, catch up on anything missed while offline.
          if (this.everSubscribed && this.loaded) {
            this.refreshEntries();
            this.refreshPrefs();
          }
          this.everSubscribed = true;
        } else if (
          status === 'CHANNEL_ERROR' ||
          status === 'TIMED_OUT' ||
          status === 'CLOSED'
        ) {
          this.realtime = 'offline';
        }
      });
    // Separate channel: if the monitor tables are missing or the subscription is refused, the
    // header still reports the log as live and entries keep syncing; the chip just stays quiet.
    if (this.sleepChannel) sb.removeChannel(this.sleepChannel);
    this.sleepChannel = sb
      .channel('sleep_status')
      .on(
        'postgres_changes',
        {
          event: '*',
          schema: 'public',
          table: 'sleep_status',
          filter: `household_id=eq.${this.caregiver!.household_id}`,
        },
        (p) => {
          if (p.eventType === 'DELETE') this.sleepStatus = null;
          else
            this.sleepStatus = { ...this.sleepStatus, ...p.new } as SleepStatus;
        },
      )
      .subscribe((status) => {
        if (status === 'SUBSCRIBED' && this.loaded) this.refreshSleepStatus();
      });
  }

  private teardown() {
    if (this.channel) supabase().removeChannel(this.channel);
    this.channel = null;
    if (this.sleepChannel) supabase().removeChannel(this.sleepChannel);
    this.sleepChannel = null;
    this.entries = [];
    this.sleepStatus = null;
    this.caregivers = [];
    this.prefs = {};
    this.timezone = undefined;
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

  async insert(
    input: {
      type: EntryType;
      started_at: Date;
      ended_at?: Date | null;
      payload: Payload;
      note?: string | null;
    },
    opts: { undoLabel?: string } = {},
  ) {
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
      updated_by: uid,
    };
    const { data, error } = await sb
      .from('entries')
      .insert(row)
      .select()
      .single();
    if (error) {
      toastError(`Couldn't save: ${error.message}`);
      throw error;
    }
    const saved = data as Entry;
    this.upsertLocal(saved);
    if (opts.undoLabel !== undefined) {
      // Undo of a fresh insert is a soft delete too: recoverable, and safe if the other phone already touched it.
      toast(opts.undoLabel, {
        undo: async () => {
          await this.softDelete(saved.id, saved.updated_at);
        },
      });
    }
    return saved;
  }

  /**
   * Conditional update: only applies if the row's updated_at still equals `expectedUpdatedAt`.
   * On a mismatch the local copy is refreshed and a ConflictError is thrown so the sheet can say so.
   */
  async update(
    id: string,
    patch: Patch,
    opts: { undoLabel?: string; expectedUpdatedAt?: string } = {},
  ) {
    const sb = supabase();
    const before = this.entries.find((e) => e.id === id);
    const expected = opts.expectedUpdatedAt ?? before?.updated_at;
    let q = sb
      .from('entries')
      .update({ ...patch, updated_by: this.session!.user.id })
      .eq('id', id);
    if (expected) q = q.eq('updated_at', expected);
    const { data, error } = await q.select();
    if (error) {
      toastError(`Couldn't save: ${error.message}`);
      throw error;
    }
    if (!data || data.length === 0) {
      // Stale token: fetch what is there now and hand it to the caller; sheets show a conflict panel.
      const latest = await this.reloadOne(id);
      throw new ConflictError(latest);
    }
    const after = data[0] as Entry;
    this.upsertLocal(after);
    if (opts.undoLabel !== undefined && before) {
      const { started_at, ended_at, payload, note } = before;
      toast(opts.undoLabel, {
        undo: async () => {
          try {
            await this.update(
              id,
              { started_at, ended_at, payload, note },
              { expectedUpdatedAt: after.updated_at },
            );
          } catch {
            /* reported by update() */
          }
        },
      });
    }
    return after;
  }

  private async reloadOne(id: string): Promise<Entry | null> {
    const { data } = await supabase()
      .from('entries')
      .select('*')
      .eq('id', id)
      .maybeSingle();
    if (data) this.upsertLocal(data as Entry);
    else this.entries = this.entries.filter((e) => e.id !== id);
    return (data as Entry | null) ?? null;
  }

  /** Soft delete with an undo toast. */
  async remove(id: string, label = 'Deleted', expectedUpdatedAt?: string) {
    const deleted = await this.softDelete(id, expectedUpdatedAt);
    if (!deleted) return;
    toast(label, {
      undo: async () => {
        try {
          await this.update(
            id,
            { deleted_at: null },
            { expectedUpdatedAt: deleted.updated_at },
          );
        } catch {
          /* reported by update() */
        }
      },
    });
  }

  private async softDelete(
    id: string,
    expectedUpdatedAt?: string,
  ): Promise<Entry | null> {
    try {
      return await this.update(
        id,
        { deleted_at: new Date().toISOString() },
        { expectedUpdatedAt },
      );
    } catch (e) {
      if (e instanceof ConflictError)
        toastError(
          'Not deleted: it changed on the other phone. Reopen it first.',
        );
      return null;
    }
  }

  /** Soft-deleted entries from the last `days`, newest deletion first, for the Recently Deleted view. */
  async fetchDeleted(days = 30): Promise<Entry[] | null> {
    const sb = supabase();
    const since = new Date(Date.now() - days * DAY).toISOString();
    const { data, error } = await sb
      .from('entries')
      .select('*')
      .not('deleted_at', 'is', null)
      .gte('deleted_at', since)
      .order('deleted_at', { ascending: false })
      .limit(500);
    if (error) {
      toastError(`Couldn't load deleted entries: ${error.message}`);
      return null;
    }
    return (data ?? []) as Entry[];
  }

  /** Bring a soft-deleted entry back. */
  async restore(e: Entry): Promise<boolean> {
    if (
      e.type === 'sleep' &&
      !e.ended_at &&
      this.entries.some(
        (x) => x.type === 'sleep' && !x.ended_at && !x.deleted_at,
      )
    ) {
      toastError('End the running sleep before restoring this one.');
      return false;
    }
    try {
      await this.update(
        e.id,
        { deleted_at: null },
        { expectedUpdatedAt: e.updated_at },
      );
      toast('Restored');
      return true;
    } catch {
      return false;
    }
  }

  async refreshSleepStatus() {
    const uid = this.session?.user.id;
    const { data, error } = await supabase().rpc('monitor_settings');
    if (uid !== this.session?.user.id) return;
    if (!error && data) {
      this.sleepStatus = data;
      this.timezone = data.timezone;
    }
    // Additive rollout: missing RPC does not prevent the existing log from loading.
  }
  async setTimezone(tz: string) {
    new Intl.DateTimeFormat('en-US', { timeZone: tz }).format(new Date());
    const { data, error } = await supabase().rpc('monitor_settings', { tz });
    if (error) throw error;
    this.sleepStatus = data;
    this.timezone = data.timezone;
  }
  async setDerivation(enabled: boolean) {
    const { data, error } = await supabase().rpc('monitor_settings', {
      enabled,
    });
    if (error) throw error;
    this.sleepStatus = data;
  }
  async pairDevice(
    name: string,
  ): Promise<{ id: string; name: string; key: string }> {
    const { data, error } = await supabase().rpc('pair_device', { name });
    if (error) throw error;
    return data;
  }
  async listDevices(): Promise<Device[]> {
    const { data, error } = await supabase()
      .from('devices')
      .select('id,name,boot_mode,last_seen_at,revoked_at')
      .order('created_at');
    if (error) throw error;
    return data ?? [];
  }
  async manageDevice(
    id: string,
    boot_mode: Device['boot_mode'] | null,
    revoke = false,
  ) {
    const { data, error } = await supabase().rpc('manage_device', {
      device_id: id,
      boot_mode,
      revoke,
    });
    if (error || !data) throw error ?? new Error('Device not found');
  }
  async savePrefs(patch: Partial<Prefs>) {
    const sb = supabase();
    const prev = this.prefs;
    const next = { ...this.prefs, ...patch };
    this.prefs = next;
    const { error } = await sb
      .from('household_prefs')
      .upsert({ household_id: this.caregiver!.household_id, prefs: next });
    if (error) {
      this.prefs = prev;
      toastError(`Couldn't save preference: ${error.message}`);
    }
  }

  /** Every entry for the household, oldest first, for export. */
  async fetchEverything(): Promise<Entry[] | null> {
    const sb = supabase();
    const { rows, error } = await this.fetchAll(() =>
      sb.from('entries').select('*'),
    );
    if (error) {
      toastError(`Export failed: ${error}`);
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
