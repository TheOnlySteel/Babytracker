# Acceptance checklist

These are proposed tests and release gates, not completed validation. Record actual results and attach evidence as implementation proceeds. Finding numbers refer to [REVIEW.md](REVIEW.md).

| ID | Test / scenario | Required result | Findings |
| --- | --- | --- | --- |
| DB-01 | Apply enum addition, then the schema migration on a disposable copy | Successful committed sequence; source-key duplicate insert behaves as intended | 6 |
| DB-02 | Read a snapshot using the chosen HTTP method | Snapshot succeeds and heartbeat behavior matches transaction access mode | 6 |
| SEC-01 | Call private helper/log directly with anon and ordinary user credentials | Access denied except for explicitly authorized surfaces | 5 |
| SEC-02 | Invoke poller using public anon key alone | Rejected before upstream API work | 5 |
| SEC-03 | Device A supplies household B's entry, caregiver, cached op, or undo target | No data exposure or mutation in B | 2, 5 |
| SEC-04 | Revoke a device and retry reads, queued writes, and duplicate lookups | All fail without effects; device shows actionable revocation state | 5 |
| SEC-05 | Run repository ignore check for real secrets headers | Real headers ignored; placeholder example remains tracked | 13 |
| POLL-01 | Start with no `sleep_status` row | Bootstrap succeeds without duplicate workers or entries | 4, 7 |
| POLL-02 | Two workers overlap; older upstream response arrives last | One admitted upstream request where intended; newer state is retained | 4, 7 |
| POLL-03 | Simulate rolling cap across local midnight, including metrics/recovery requests | Counter reset does not permit quota overspend | 7 |
| POLL-04 | Inject 401, 403, 429, 5xx, invalid JSON, and transport timeout | Correct distinct health state and bounded retry/backoff | 1, 4, 7 |
| FRESH-01 | Stop upstream polling while database/device snapshots remain healthy | Phones, dashboard, dimmed screen, and lamp visibly show stale source | 1 |
| SLEEP-01 | Sleep 3 min then cry; sleep long enough; stir twice during a nap | Provisional/confirmed rules produce the expected entries without duplication | 3, 4 |
| SLEEP-02 | Awake 2 min then crying 3 min | Combined non-sleep episode follows the documented debounce rule | 4 |
| SLEEP-03 | Add note/place edit during an automatic running nap | Annotation preserved; automatic closure still works | 3 |
| SLEEP-04 | Manually End while crib still reports sleeping; replay recent history | Manual time preserved; same episode is not recreated | 3, 4 |
| SLEEP-05 | Manual crib timer precedes automatic eligibility | One linked/adopted/suppressed result; no duplicate counted coverage | 3 |
| SLEEP-06 | Human correction races automatic closure | Conditional update preserves the newer human decision | 3 |
| SLEEP-07 | Sleeping before outage; sleeping with new `since` after recovery | Missing interval is unknown or reconciled; no invented continuous sleep | 1, 4 |
| SLEEP-08 | Dismiss, replay/backfill, restore while another sleep runs | Dismissal survives replay; restore collision is understandable and safe | 3, 4 |
| SUM-01 | 20:30–07:00 entry; view next morning's Today | Seven hours overlap with midnight–07:00; History placement remains coherent | 8 |
| SUM-02 | One night with segments starting 20:30, 01:00, 04:00 | One night grouping; wake count matches the defined episode rule | 8 |
| SUM-03 | Rolling 24h boundary, two nights, DST change, and incomplete data | Correct overlap; no phantom inter-night wakings; unknown time identified | 8 |
| WEB-01 | Open running and completed sleep from each Card/History path | Correct Sleep sheet; never Pump fallback | 12 |
| WEB-02 | Reconnect/foreground and logout after sleep status changes | Catch-up on return; old household state cleared on logout | 1, 12 |
| WEB-03 | Pad starts a pump; phone stops it after 7 min | Accurate elapsed end time; pump banner available; no forced 20 min | 10 |
| WEB-04 | Total-only pump volume and phone edits of pad entries | No false left/right split; provenance survives labels/edit/export | 10 |
| DEV-01 | Parse null IDs, absent optional values, escaped names, unknown fields | Correct typed result or explicit validation failure; no phantom timer | 9 |
| DEV-02 | Stall network connection/read while tapping Stop/mute/Back | Responsive UI; no blocked touch handling | 9 |
| DEV-03 | Exercise every retained screen, idle return, C button, short/long tap, reboot | Data exists for each view; controls unambiguous; drafts/mode preserved | 11 |
| OP-01 | Same op retries concurrently, including acknowledgement loss | One committed effect; repeat returns original result | 2 |
| OP-02 | Same operation ID is reused with different content | Rejected; original operation/result unchanged | 2 |
| OP-03 | Diaper tapped at 02:00 offline, delivered 02:20 after reboot | One entry at original event time, with defined clock confidence | 2 |
| OP-04 | Queue is full; network flaps during flush | No overwrite/lost actions; visible refusal/retry status | 2 |
| OP-05 | Cancel before upload; Undo after upload; Discard a completed long feed | Each follows its explicit semantics and version checks | 2, 11 |
| TIMER-01 | Two pads/phones race Start, Switch, and Stop | Database invariants hold; conflicts visible; no silent action loss | 2, 3, 10 |
| TIMER-02 | Later release: offline Start → Switch → Stop, reboot, replay | Correct entry identity, segment times, duration, and caregiver provenance | 2 |
| TIMER-03 | Another caregiver edits during replay | No blind overwrite or discarded pending intention | 2 |
| REL-01 | Old phone client remains open when new type is introduced | Compatible display/recovery or explicit required update; no wrong editor | 12 |
| REL-02 | Deploy/rollback with derivation flag; run deployment setup twice | No duplicate cron; reversible enabling; collected history retained | 7, 12 |
| REL-03 | Old direct pollers present during proposed cutover | Deliberate handoff to one upstream poller within budget | 7 |
| CI-01 | Run app tests/check/build plus committed new harness | New deriver tests actually discovered; fixtures and harness reproducible | 12 |
| HW-01 | Build pinned firmware; test two actual units, TLS, power loss and Wi-Fi recovery | Recorded hardware results; no web-only claim of firmware validation | 9, 11 |

For every release, record the Git commit, environment, tests executed, failures or skipped tests, and remaining manual checks. Do not mark these gates passed based only on proposed test code or mocked API responses.
