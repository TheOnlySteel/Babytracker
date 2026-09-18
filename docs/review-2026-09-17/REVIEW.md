# BabyTracker / CradleWatch integration review

Reviewed 17 September 2026 (Pacific), against GitHub `main` at `458e012851f2c367cd04e41abe61816f3675dbfe`.

**Verdict: the architecture is worth building, but the plan should be revised before implementation.** Central polling, ordinary sleep entries, shared database timers, and revocable device keys are good choices. The current proposal contains several executable contradictions and leaves data ownership, offline replay, and missing observations underspecified. Those are the parts that determine whether parents can trust the log.

This is a review, not an implementation or deployment. The repository currently contains the existing Svelte app, migrations 0001–0003, standalone CradleWatch firmware, and the integration plan. Migration 0004, the poller, sleep UI, device RPCs, and NurseryPad firmware are not implemented here.

Sources: [integration plan](https://github.com/TheOnlySteel/Babytracker/blob/458e012851f2c367cd04e41abe61816f3675dbfe/docs/core2-integration-plan.md), [CradleWatch firmware](https://github.com/TheOnlySteel/Babytracker/blob/458e012851f2c367cd04e41abe61816f3675dbfe/CradleWatch/CradleWatch.ino), [original app specification](https://github.com/TheOnlySteel/Babytracker/blob/458e012851f2c367cd04e41abe61816f3675dbfe/baby-tracker-spec.md). The referenced Core2 design canvas is not in the checkout, so its screen layouts were not visually reviewed.

## Findings to resolve before shipping

### 1. P1 — Fresh snapshots can conceal a dead Cradlewise feed

**Evidence:** plan lines 124–135, 250–256; firmware lines 582–588, 753–769, 782–811, and 976–980.

The snapshot includes `server_now` and the crib's state-change time, but omits `sleep_status.observed_at`. The plan also carries over the existing stale-data banner unchanged. Today, a successful HTTP request means a successful Cradlewise poll. After integration, a successful device request only means Supabase is reachable. The poller could be down for hours while each device continues to receive successful snapshots of “sleeping.” `since` cannot establish freshness: the baby may legitimately remain in one state for hours.

**Change:** return source observation time, upstream response timestamp, poll health/error reason, and snapshot time separately. Show “Crib update 12 min old” on the home screen and monitor, independently of Wi-Fi or Supabase connectivity. Change stale/unknown lamp output to a neutral indication and mark incomplete statistics; do not keep accumulating confirmed sleep through unobserved gaps. The existing lamp and dimmed-night renderers have no call to `drawBanner()`, and night dimming does not check freshness or Wi-Fi. Inheriting them unchanged preserves an existing stale-display blind spot.

**Acceptance:** keep Supabase responding while deliberately stopping the poller. Both phones and both pads must flag stale crib data. An old `sleeping` response must not be presented as a current observation.

### 2. P1 — Offline actions need event times, identities, and dependency handling

**Evidence:** plan lines 139–154 and 259.

The persisted queue stores `{client_id, op, args}`, but the operation contract contains no occurrence time. A diaper tapped at 02:00 and uploaded at 02:20 will be dated at reconnect unless an additional timestamp is defined. A feed started, switched, and stopped offline has a harder problem: subsequent operations need the entry ID and version produced by the still-unacknowledged start. Even online, two quick operations can carry the same snapshot version.

Dropping a conflicting operation and refreshing silently loses a parent's action. An eight-slot ring also needs an explicit full-queue behavior. Undo currently has a 30-second server window and only means “delete the row created by this operation”; that does not implement undo of a switch or stop, nor the promised Save/Discard feed summary after a longer session.

**Change:** define a durable operation envelope with device ID, stable operation UUID, occurrence time, clock confidence, local entry/session identity, and predecessor relationship. Acknowledge operations in order and map server IDs/versions before executing dependents. Commit the mutation and its idempotency result in one transaction. Authenticate before reading cached results; scope duplicate lookups to the authenticated device and reject UUID reuse with a different request body. Preserve unresolved conflicts visibly. A full queue should refuse a new action clearly, never overwrite an older one. Handle cancel-before-upload and undo-after-acknowledgement explicitly.

**Simpler first release:** allow queued one-shot bottle/diaper logs, but require connectivity to start/switch a timer. Add complete offline timer sequences only once their replay rules are tested. This is a substantial reduction in complexity for a household tool.

### 3. P1 — Human edits and automatic sleep ownership conflict

**Evidence:** plan lines 50–62, 178–184, 217–222.

Saving any human edit marks an auto row `edited=true`, after which the deriver never updates it. Add a note while the baby is still sleeping and automatic closure is disabled. The open row then blocks every subsequent sleep through the unique index. Conversely, the banner/device “End” paths only specify writing `ended_at`, without the same override rule.

There is also no rule for a manual timer already running when an auto nap becomes eligible five minutes later. The unique index rejects the automatic insert, but rejection is not reconciliation. Once the manual row is closed, backfilling an overlapping auto row could count the same period twice.

**Change:** separate editable annotations from timing ownership. A note/place edit should not disable automatic closure. Define explicit timing overrides or a manual takeover action, and preserve that decision across sheet, banner, pad, recovery, and replay paths. Define how a manual crib timer is linked to an observed crib episode, or how the overlapping automatic proposal is suppressed. The poller must apply edits conditionally in the database, not merely read `edited=false` and later overwrite a concurrently changed row.

**Acceptance:** note-only edit while sleeping; manually end while crib still reports sleeping; manual nap followed by auto detection; phone edit racing a poll; dismiss and restore while another sleep is open. Each must leave one understandable result.

### 4. P1 — Polling current status alone cannot produce a complete sleep history

**Evidence:** plan lines 170–184 and 275.

Suppose the last observation was sleeping at 01:00, polling fails, and the next observation is sleeping since 03:30. The intervening waking has disappeared from the transition stream. With the proposed rules, an open sleep can span the gap and overstate sleep. Short wakes between polls are also unobservable. `unknown` behavior, malformed responses, upstream time corrections, and out-of-order poll completions are not specified.

Five-minute debounce is a reasonable provisional heuristic, but “awake 2 min → crying 3 min” needs to count as one five-minute non-sleep interval, rather than restart the clock on every status label. Brief waking periods retained inside a sleep row also mean its wall-clock duration is not necessarily actual asleep time.

**Change:** move bounded `/sleep/c-chart` reconciliation from optional phase 5 into the initial sleep release. Preserve normalized source observations separately from user-facing entries. Show live inferred sleeps as provisional, then reconcile recent history after recovery and at a daily boundary, preserving human overrides and dismissals. Do not assume example session IDs are stable across queries without verifying that contract. Explicitly distinguish unknown time from awake time. Cradlewise provides historical raw events and day aggregates for this purpose. [Official sleep-history reference](https://integrations.cradlewise.com/documentation#ep-cchart).

### 5. P1 — The authorization boundary is incomplete

**Evidence:** plan lines 109–124, 139, 156, 162–165.

There are three separate issues:

- `device_ops` has neither RLS nor explicit direct-access revocations. On a project with Supabase's permissive default grants, ordinary Data API callers could reach the operation log without a device key. Exposure depends on the live grants, which were not inspected; the missing protection in the migration is definite.
- Revoking function execution only from `anon` and `authenticated` does not remove PostgreSQL's default `PUBLIC` execution grant. Make revocation from `PUBLIC` explicit, then allow only the intended entry points. Keep authentication helpers and the operation log in an unexposed schema where practical.
- The cron example authenticates the poller using the public anon key. That does not distinguish the scheduler from a visitor to the app. A caller could repeatedly invoke the service-role worker and consume the Cradlewise request allowance.

**Change:** use a server-only scheduler secret; enforce it inside the worker. Device RPCs must authenticate first and explicitly constrain every entry, caregiver, undo target, and cached result to the device's household. `SECURITY DEFINER` moves this responsibility into the function; the existing user RLS is not a substitute. Add negative authorization tests and explicit table/function grants in migrations.

This is compatible with the proposed direct PostgREST device path; a separate device server is not necessary. [Supabase grants/RLS guidance](https://supabase.com/docs/guides/api/securing-your-api), [PostgREST function privileges](https://postgrest.org/en/stable/explanations/db_authz.html), [service-to-service function authentication](https://supabase.com/docs/guides/functions/auth#service-to-service-calls).

### 6. P2 — Three SQL/transport details fail as written

**Evidence:** plan lines 43–52, 124, 178, 254, 283.

| Defect | Consequence | Repair |
| --- | --- | --- |
| `GET device_snapshot` calls authentication that updates `last_seen_at` | PostgREST GET runs in a read-only transaction | Use POST with a VOLATILE snapshot function, or make GET genuinely read-only and send a separate heartbeat |
| Add enum value and use `'sleep'` in an index in the same transaction | The new enum value cannot be used before commit | Commit the enum addition in a separate migration before the schema/function migration |
| Partial unique index on `source_key`, but `ON CONFLICT (source_key)` without the predicate | Index inference fails | Use a regular nullable unique constraint, or match `WHERE source_key IS NOT NULL` in the conflict target |

The enum caveat is mentioned in the plan, but it should be resolved in the migration design rather than left to a production error. Consider uniqueness on `(household_id, source_key)` and canonicalize timestamps so equivalent ISO strings cannot create distinct identities.

These are checked against documented database behavior, not executed against the live project. [PostgREST access modes](https://postgrest.org/en/stable/references/transactions.html#access-mode), [PostgreSQL enum transaction rule](https://www.postgresql.org/docs/current/sql-altertype.html#SQL-ALTERTYPE-NOTES), [partial-index conflict inference](https://www.postgresql.org/docs/current/sql-insert.html#SQL-ON-CONFLICT).

### 7. P2 — The poll budget is measured against the wrong window

**Evidence:** plan lines 34 and 170–173.

Cradlewise documents rolling limits, including the combined 2,880-request daily allowance. A counter reset at local midnight cannot enforce that allowance. Adaptive polling will usually create headroom, but does not guarantee it; sustained active polling plus 48 daily metric calls already exceeds 2,880. During migration, old standalone CradleWatch units and the new server also share the same token budget. [Official rate limits](https://integrations.cradlewise.com/documentation#rate-limits).

**Change:** enforce request admission centrally using a rolling request ledger or equivalent limiter and the response headers. Reserve capacity for reconciliation; defer metrics before live status. Count all relevant attempts, set timeouts/backoff for 5xx and parsing failures, and represent 401 versus subscription-related 403 distinctly. Add a short database lease so concurrent Edge invocations cannot both poll or overwrite a newer observation. A cron tick that only enqueues an HTTP request does not serialize the remote worker's entire lifetime.

Keep `requests_today` as a display metric if useful, but do not use it as the quota gate. At cutover, stop or reflash direct-polling monitors before enabling the server poller.

### 8. P2 — Sleep totals, night grouping, and waking counts need different rules

**Evidence:** plan lines 138, 201–205, 225, 273; `src/lib/data/summary.ts` lines 26–29 and 49.

The current `inWindow` selects entries by start time. Reusing that for sleep makes an uninterrupted 20:30–07:00 night contribute zero to the following calendar day's “Today” total, despite seven hours of overlap. Grouping History rows under their start date can be a legitimate display choice; it should not dictate interval totals for Today or Last 24 Hours.

The night-row-minus-one waking formula also breaks across midnight. Rows starting 20:30, 01:00, and 04:00 belong to one night, but are split between two calendar dates. Counting rows in each date undercounts the night's wakings; a multi-night window can create phantom wakings between nights.

Finally, the lamp score counts all awake/crying/away transitions, whereas the sleep deriver absorbs short wakes. Those are different metrics. The phase-3 requirement that the lamp match the web Summary is therefore not well-defined.

**Change:** use interval overlap for elapsed sleep totals, a stable night identifier for night summaries, and explicitly defined wake episodes. Offer “Last night” as a separate view from calendar Today/24h. Keep a clearly named crib-settled percentage if wanted, but do not equate it with inferred asleep minutes. Use the household's IANA timezone and returned day boundaries; Cradlewise's default day is 08:00–08:00 and can vary historically. [Official day-boundary reference](https://integrations.cradlewise.com/documentation#day-start).

Test midnight, a rolling-window boundary, a DST transition, sleep beginning before the loaded window, and a partial night with missing observations.

### 9. P2 — Do not carry the JSON picker or blocking transport over unchanged

**Evidence:** plan lines 250, 254, 259; firmware lines 203–213, 271–279, 947–969.

The firmware's `jsonQuoted` searches for the next quote after the colon, regardless of the value's type. In a planned snapshot such as `{"bf_id":null,"bf_side":null,"sleep_id":"nap-1"}`, asking for `bf_id` yields the string `bf_side`. Escaped quotes in a caregiver name are also parsed incorrectly. Flat JSON does not avoid these cases. A faithful JavaScript port reproduced both errors; this was not a native ESP32 execution.

Use a bounded JSON parser, validate nullable IDs and numeric fields, and include a protocol version. Preserve the exact opaque row-version token; do not round a PostgreSQL timestamp through a seconds-only date parser.

The existing HTTPS code is synchronous with 8-second connect and 12-second read timeout settings. Running the replacement network request in the same loop will stall touch sampling, timer redraws, and mute controls under poor connectivity. A five-millisecond delay at the bottom of the loop does not make it responsive during that call. Move network work off the UI loop and deliver completed results back through a queue.

### 10. P2 — The phone pump path does not meet the new device contract

**Evidence:** plan lines 148, 222, 274; `PumpSheet.svelte` lines 14–24, 45–59, 92–101; `derive.ts` lines 34–37.

The new pad can start a running pump, but the existing phone sheet is manual-duration only. Opening a running pump defaults its duration to 20 minutes; saving closes it at start plus 20 minutes rather than the current elapsed time. The planned banner list includes breastfeed and sleep but omits pump.

Putting an unsplit pump total in `left_ml` is also false data: current labels and summaries treat it as left-side output. The proposal's statement that the sheet shows “total” is not supported by a planned modification to that sheet.

**Change:** implement pump timer display and Stop on the phone before device pump writes ship. Add an explicit total-only volume representation or unknown-side volume, updating display, summaries, validation, and export together. Preserve `payload.via` on phone edits: Bottle, Diaper, and Pump sheets currently rebuild their payloads and would strip it. A dedicated provenance field would avoid mixing transport metadata into each clinical/activity payload.

### 11. P2 — The snapshot and operations do not yet support all promised screens

**Evidence:** plan lines 127–152 and 257–265.

The snapshot lacks today's sleep rows and the seven-day aggregates needed by Sleep Log/Stats. It also lacks several values the existing CradleWatch screens show: bounce/music status and the full day-metric set. The plan must either deliberately simplify those screens or provide their data. Specify the six Stats cells, their time windows, and how manual sleep affects them.

Likewise, there is no explicit operation for the Feed Summary's long-session Discard action or saving amendments after Stop. Decide whether Stop is the committed save; if it is, label the next screen as confirmation and expose a deliberate correction/delete path.

There are input conflicts to resolve: the C button is globally Dashboard but also seven-day averages in Stats; “tap anywhere” exits Dashboard but competes with mute/cry acknowledgement and long-press lamp entry. Define precedence and fire tap navigation on release only after ruling out a long press. Preserve drafts across idle timeouts, and let each device persist an appropriate home/dashboard/lamp preference. A dedicated lamp should remain a lamp after a reboot.

### 12. P2 — The file checklist and rollout order miss compatibility paths

**Evidence:** plan lines 207–220, 237–238, 271–277; `Card.svelte` lines 34–35 and 53–55; `store.svelte.ts` lines 65–71, 122–132, 178–181.

Adding sleep to the Card's running filter is insufficient: its local `editSheet` fallback routes unknown types to Pump. The separate History mapper is listed in the plan, but the Card mapper is not. Add sleep to the store's out-of-window running fetch and replace/remove logic, too. Refresh sleep status after foregrounding/reconnect and clear it on logout. Receiving Realtime events is not a substitute for catching up after a disconnected interval.

Type checking will catch some union exhaustiveness errors, but not ternary fallbacks or switches with defaults. Add explicit fixtures rather than treating `svelte-check` as a complete checklist. The current Vitest include patterns also omit `supabase/functions/**/*.test.ts`; wire the new tests into CI deliberately. The referenced `scratchpad/pw` harness is not committed in this checkout.

The phase order enables new sleep writes before deploying a web app that understands them. Older phone clients can also remain open after a deploy. Apply additive migrations with automatic writes disabled, deploy compatible readers and safe unknown-type handling, then enable the poller. Test rollback by disabling derivation without deleting the collected source data.

### 13. P2 — `secrets.h` is not actually ignored

**Evidence:** `CradleWatch/README.md`, `CradleWatch/secrets.example.h`, and the root `.gitignore`.

Both setup texts say `secrets.h` is gitignored. `git check-ignore --no-index CradleWatch/secrets.h` returns no match in this checkout. The repository is public, so following the documented setup and later adding the directory could publish Wi-Fi credentials and the Cradlewise/device key.

Add an explicit pattern for the real secrets headers before the next setup/flash workflow. No actual secret header was present in the reviewed tracked files; this finding is about the false protection promised by the setup instructions.

## What I would keep, simplify, and defer

Keep one server poller and one shared entry store. This avoids depending on a powered monitor for logging and makes extra displays inexpensive. Keep the reversible human correction workflow and conditional writes. Keep the CradleWatch visual language and large, direct feed/diaper controls.

The strongest argument for the current plan is its scale: one household does not need a generalized device platform or a full event-sourcing system. I agree. A small operation ledger, a handful of transactional RPCs, a source-history table, and clear invariants are enough. These findings do not justify introducing another hosted service.

I would make sleep capture plus a read-only monitor the first useful release. Then ship one-tap bottle/diaper logging. Shared timers and offline timer recovery are a separate, harder release. Where phone and pad mutate the same timers, consider shared transactional database operations so segment and stop semantics cannot drift between TypeScript and SQL implementations.

Product improvements with a clear payoff:

- Show “Last night,” wake episodes, and last-feed/last-diaper age prominently. They answer practical questions better than an unexplained percentage.
- Make pending, saved, failed, stale, and unknown visibly different. A small red dot alone does not tell a tired user whether a feed was recorded.
- Show source attribution as “Cradlewise, adjusted” after a human correction, rather than losing the distinction when `edited` becomes true.
- Offer a completed manual sleep entry directly, as well as Start Nap. Logging an earlier nap should not require creating and immediately stopping a live timer.
- Keep caregiver selection obvious, but record the device separately. A selected name is attribution by the household, not proof that person authenticated the action.
- Treat the lamp as an optional household workload cue, with a visible missing-data state. Its thresholds are design choices, not validated measures of sleep quality.
- Make current hardware builds reproducible by pinning Arduino core/library versions and adding a compilation job. Run physical touch, Wi-Fi-loss, reboot, and power-loss checks before replacing the working firmware.

The stated 260 MB/month egress estimate is too underspecified to validate. Three devices polling every 15 seconds make 518,400 snapshots per 30 days. At an assumed 1.5 kB JSON body, that is about 778 MB of response payload before other traffic. Measure the actual response size and current account allowance. At this household scale, correctness and query simplicity matter more than optimizing away a JSON parser or an API field.

The older specification still mentions a phone offline write queue, pump live timer, optional Growth charts, Siri shortcuts, and a Combo sheet. The current app has no phone write queue and no live pump controls. The new plan addresses neither completely. I would prioritize honest phone offline/pending behavior and pump parity over Growth, Siri, temperature sensors, or seven-day device charts. Keep those later features explicitly deferred. Do not add a hard-delete purge until persistent dismissal/tombstone behavior is defined; deleting a suppressed source identity would defeat “never re-create this nap.”

## Revised delivery order and proof required

1. **Contract and fixtures:** capture redacted real API examples; define freshness, sleep ownership, night boundaries, operation envelopes, and timezone handling. Add the missing ignore rules. Commit the test harness.
2. **Backend with writes disabled:** split migrations; establish explicit privileges, scheduler authentication, poll serialization, quota admission, and source-history collection. Run real PostgREST and RLS tests against a disposable database.
3. **Sleep on the phones:** implement interval-aware summaries, manual entry/correction, source health, recovery, and history reconciliation. Deploy compatible clients, then enable automatic entries. Compare at least one full night and several naps with the source app, documenting expected differences.
4. **Read-only NurseryPad:** proper JSON parsing, responsive UI during networking, fresh/stale/unknown states, complete screen data, and device revocation. Keep one existing monitor available during cutover and ensure only the intended poller uses the upstream token.
5. **Device writes:** bottle/diaper first; then shared feed/pump/sleep timers with deliberate conflict and undo behavior. Add offline timer chains only after reconnect/reboot tests demonstrate correct original times and exactly-once effects.

Release tests should include concurrent duplicate operations, the same UUID with different contents, two devices racing Start/Stop, a revoked key, a foreign household ID, queue saturation, power loss after the server commits but before acknowledgement, stale upstream data with healthy Supabase, a midnight/DST night, and human edits during reconciliation. Mocked HTTP 409 tests alone cannot establish database concurrency or authorization correctness.

## Verification performed and limits

- Read the current GitHub plans/PR context, all new CradleWatch source, existing migrations, and the affected app data/UI paths.
- Verified quota/time-boundary contracts in the live official Cradlewise documentation and database/authentication behavior in official PostgREST/PostgreSQL/Supabase documentation.
- Existing app: **20 tests passed; 3 private-fixture tests skipped.** Svelte check: **0 errors, 0 warnings.** Production build succeeded with a non-fatal unmatched prerender glob warning. Used existing dependencies after confirming an identical `package-lock.json`; these checks were not a fresh install.
- Reproduced the null/escaped-string picker errors through a JavaScript port and calculated the midnight interval counterexample. Outputs: [reproductions.json](evidence/reproductions.json).
- Both bundled PEM certificates parsed successfully with OpenSSL and are within their encoded validity periods. The live server-to-device TLS chain was not tested.
- Did not execute the proposed migrations, inspect production database grants, call the family's authenticated Cradlewise API, or flash/compile/test physical ESP32 hardware. The planned integration cannot pass end-to-end tests yet because its implementation is absent. A green web CI run is evidence about the existing app, not about the new integration.

**Recommendation:** approve the overall direction, revise the contracts above, and replace the session-count estimates with the acceptance gates. The difficult work is preserving correct behavior through edits, outages, and retries; twelve new screens should follow that foundation.
