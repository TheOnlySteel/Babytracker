# Proposed implementation approach

These are design recommendations to resolve the findings in [REVIEW.md](REVIEW.md). They are not implemented migrations or confirmed production behavior. Prefer the smallest changes that establish the invariants below. Preserve the existing app's conflict handling, draft protection, recovery, accessibility, and import behavior.

## 1. Separate three kinds of information

**Source observations:** what Cradlewise reported, when it says the state started, when it generated the response, and when the integration received it. Retain enough normalized history to reconcile recent sleep after an outage. Include the response's source timezone/day boundary when applicable. Verify the identity and ordering of upstream events against real redacted fixtures.

**User-facing sleep entries:** the editable log shown on phones and devices. Entries can have automatic provenance while retaining a parent's corrections. Record whether timing is still managed automatically; a note, place, or kind edit alone must not disable automatic closure. Human timing changes should be explicit and must survive later reconciliation.

**Current device snapshot:** a compact, versioned read model derived from the database. Its delivery time and the freshness of its crib data are independent. Include the fields needed by every retained screen, with clear nulls and documented units.

This can fit the proposed `entries`, `sleep_status`, `sleep_transitions`/source-history, `devices`, and `device_ops` structure. Exact column names are implementation choices. Keep a persistent dismissal identity if a future purge removes soft-deleted entries.

## 2. Define sleep behavior before writing the deriver

Use a deterministic state machine with explicit pending-sleep, confirmed-sleep, pending-wake, and unknown-gap handling. Five-minute debounce may remain an initial heuristic; label it as such. Combine awake/crying/away into one non-sleep episode for debounce rather than resetting solely because the status label changes. Define the pickup exception and test it explicitly.

Automatic entries are provisional until reconciled with historical source data. Reconcile a bounded recent interval on recovery and at a daily boundary. Apply entry changes atomically and conditionally, preserving human overrides. Do not infer uninterrupted sleep through a gap only because the first and last observations say sleeping.

For a manual timer overlapping automatic detection, choose and document one policy: link/adopt the matching manual crib session, or suppress the overlapping automatic proposal. Do not silently create overlapping counted intervals. Decide how an explicit manual Stop suppresses further derivation of the same source episode, and when a later episode becomes eligible again.

A user who explicitly takes over a still-running automatic timer must see that automatic closure has stopped. Ordinary note edits do not constitute takeover. Restore must detect collisions with another running sleep and give a useful resolution.

For elapsed totals, calculate intersection with the requested interval:

```text
max(0, min(entry_end_or_clock, window_end) - max(entry_start, window_start))
```

Use half-open reporting windows where practical to avoid counting a boundary twice. Distinguish wall-clock session length, inferred asleep time, unknown time, and the optional crib-settled percentage. Do not silently label them as the same quantity. Define how overlapping manual/automatic coverage is deduplicated before summing.

Keep History placement by start date if useful, but calculate Today/24h sleep using overlap. Group night episodes with a stable night identifier, and define which wake episodes count and whether final morning rising is excluded. A “Last night” view should keep post-midnight segments together. Use an IANA timezone for server/web date arithmetic; keep the ESP32 POSIX TZ string as a separate representation rather than assuming the strings are interchangeable.

## 3. Make polling private, serialized, and quota-aware

- Protect the Edge Function with a server-only scheduler credential validated by the function. Do not use the public anon key as proof that the caller is the scheduler.
- Acquire a short lease atomically before upstream work. Reserve request capacity atomically so concurrent invocations cannot overspend it. Apply observations only if they are not older than the accepted observation/version.
- Enforce rolling endpoint/global limits and respect upstream rate-limit headers. Reserve capacity for reconciliation and degrade optional metric refresh first. The local calendar-day counter is informational.
- Handle transport errors, timeouts, 5xx, invalid response shapes, 401, 403, and 429 explicitly. Keep source health separate from database health.
- Run polling/source capture with automatic entry creation disabled during rollout. Enable derivation only after compatible readers have shipped.

Include a bootstrap path for an empty status table, a lease-expiry/retry path after a crash, and idempotent observation processing. Avoid duplicate scheduled jobs when deployment scripts are rerun.

## 4. Complete the device API contract

Default recommendation: use POST for a VOLATILE `device_snapshot` function if it updates `last_seen_at`. Keep the operation log and authentication helpers private, remove default `PUBLIC` execution access, and grant only intended RPC entry points. Explicitly constrain targets and caregiver selections to the authenticated device's household.

Snapshot contract should define at least:

- Protocol version; snapshot/server time; source observation and response times; source health/reason.
- Current crib state/since, retained bounce/music indicators, and timezone/night settings.
- Running timer IDs, exact opaque versions, source/ownership, relevant segment state, and elapsed values with units.
- Caregiver choices, recent feed/diaper/pump information, and defaults required by the forms.
- Data for today's sleep list and the exact Stats cells, either in the main snapshot or an on-demand bounded read. Defer seven-day device statistics if their contract is not included.

Use a real JSON parser and reject invalid shapes. Missing values must remain missing; do not turn null into zero or an arbitrary string. Do not parse/reformat concurrency tokens through a seconds-resolution clock.

## 5. Specify mutation and replay semantics

A recommended conceptual operation envelope:

```text
protocol_version
client_operation_id      stable across retries
operation
occurred_at              original action time, UTC when known
clock_quality            whether that timestamp is trustworthy
client_session_id        identity for a local timer sequence, where needed
predecessor_operation_id ordering/dependency for a timer chain, where needed
expected_version         for an existing acknowledged row
arguments                validated operation-specific values
```

Resolve the device and household from authentication, not caller-supplied IDs. Authenticate before looking up a cached result. Scope idempotency to device plus operation ID and bind it to the request contents. Commit the data change and recorded result in the same transaction; an acknowledgement retry must return the committed result.

Define explicit outcomes: applied, duplicate with the original result, conflict with current state, validation failure, revoked/not authorized, and retryable failure. A device must distinguish terminal and retryable failures. Preserve conflicts for user resolution rather than silently discarding them.

For the first write release, queue only one-shot logs offline. Persist the queue before showing “saved locally,” retain original times, acknowledge/remove only after success, and refuse new writes visibly when full. Define behavior if no trusted wall clock is available after reboot.

For later offline timer support, replay dependencies in order, resolve local IDs to server rows, and use newly acknowledged versions. A foreign edit must still produce a conflict; automatic rebasing must not overwrite another caregiver. Test a crash after server commit but before device acknowledgement.

Define Cancel, Stop, Save, Delete, and Undo separately. Cancel an unsent local log locally. Undo after upload needs a version-aware compensating operation. A completed 20-minute feed cannot rely on a 30-second creation-undo window for Discard. If Stop commits immediately, call the subsequent screen confirmation rather than implying the entry is still unsaved.

## 6. Complete phone parity before device timers

Add live pump support, accurate elapsed Stop, and a pump banner on the phone before accepting pump starts from pads. Represent total-only pump output explicitly; do not assign unknown-side volume to `left_ml`. Update labels, summary, export, and tests together.

Audit all entry-type dispatchers, including Card's local mapper, the store's running-entry queries, History, export, summary, and recovery. Add safe behavior for unknown types. Refresh crib state after reconnect/foreground and clear it when the session ends. Preserve device/source provenance during every phone edit.

Keep “Cradlewise, adjusted” visible for corrected entries. Let parents log a completed earlier nap directly. Preserve pending form input during timeout/navigation and make pending, saved, failed, and conflicting states legible.

## 7. Firmware and controls

Move HTTP work off the input/render loop. A disconnected network must not freeze Stop, mute, Back, or the clock. Validate results and pass them back to the UI through a bounded queue.

Define touch precedence: wake screen, acknowledge/mute an alert, long press, then ordinary navigation. Resolve the C-button Dashboard/weekly-stats conflict. Persist each unit's intended hub/dashboard/lamp behavior so an off-shift lamp remains one after reboot. Stale/unknown source data must be visible on every mode, including the dimmed screen.

Pin the board/core/library toolchain and compile firmware in CI. Add the real `secrets.h` ignore pattern before setup; retain example files with placeholders. Verify TLS trust on the actual board without disabling validation as the normal recovery strategy.

## 8. Release sequence

| Release | Scope | Gate |
| --- | --- | --- |
| A — Contracts and fixtures | Revise plan, capture redacted payloads, wire test harness, protect secrets | Findings have explicit disposition and the behavior above is documented |
| B — Backend collection | Split migrations, explicit grants/RLS, private scheduler, quota/lease, source history; derivation disabled | Disposable-database and RPC tests pass; source collection has health visibility |
| C — Phone sleep | Compatible readers, manual entry/edits, overlap-aware reports, reconciliation, staged auto-entry enablement | Full-night and nap comparison; old-client and outage tests |
| D — Read-only NurseryPad | Snapshot, parser, responsive transport, hub/dashboard/lamp, revocation | Physical two-device, stale-source, Wi-Fi loss, reboot, touch tests |
| E — Simple device logs | Bottle and diaper operations, durable one-shot offline queue | Correct original timestamps, duplicate retry, queue-full and undo tests |
| F — Shared timers | Feed/pump/sleep parity and explicit conflict handling | Phone/pad handoff and concurrent Stop/Start tests |
| G — Extended offline timers | Dependent operation replay | Complete offline Start/Switch/Stop survives reboot and conflicts without data loss |

Keep WebSockets, device seven-day charts, temperature sensors, Growth charts, and Siri as later options. Preserve them as explicit deferred scope instead of implying the integration completes them. A phone offline queue is also a separate deliverable; the device queue does not supply one.

Cutover must leave one upstream poller using the token. Stop or reflash older direct-polling CradleWatch units before enabling the shared server poller. Rollback should disable new automatic writes, retain source observations and parent corrections, and leave ordinary phone logging available.
