# Integration test ownership

The nine stages in [bitcoin/bitcoin#36289](https://github.com/bitcoin/bitcoin/issues/36289)
work as test ownership boundaries. The fixture from
[gui-qml#939](https://github.com/bitcoin-core/gui-qml/pull/939) starts the production
application and a real regtest node for every test function or data row. Tests can
drive QML controls, inspect the actual models and signals, and compare their state
with Core RPC results without expanding the Python automation bridge.

| Stage | Directory | Current integration coverage | Remaining stage acceptance work |
| --- | --- | --- | --- |
| 1. Foundation | `foundation` | Startup, shutdown, application lifetime, pending backend work | Packaging, platform startup and fatal-exit checks |
| 2. Node setup and shell | `node_setup` | Navigation, settings, language and units, About-link confirmation/cancellation, proxy validation/draft discard/save, window controls, live block-clock history/reorganizations, synchronization and network pause | Pre-init onboarding, persisted settings and restart journeys; tray and native menus |
| 3. Node tools | `node_tools` | Core network notifications and modal messages, live debug log, console commands/autocomplete/layout, blocks-only navigation | Multi-node peer/ban/proxy/traffic journeys and broader mempool inspection |
| 4. Wallet management | `wallet_management` | Name validation, create/encrypt/unlock/relock, close/reload and model destruction, selector row actions/load errors, password-context changes, encrypted backup/restore and invalid-backup UI | Legacy migration, cross-version compatibility and native file dialogs |
| 5. Wallet activity | `wallet_activity` | Activity search/type/date/amount filters, CSV export and display units, fulfilled-request associations; receive requests, private notes, labels propagated to activity/address/request models, address types, sign/verify, address-to-request editing, replacement/confirmed receipt totals | Broader transaction history, multi-node funding and QR journeys |
| 6. Wallet sending | `wallet_sending` | Custom fees under two relay/wallet policies, review/broadcast/mempool fees, coin dialog apply/cancel and locks, maximum/fixed sweeps, ownership exceptions, multi-recipient drafts, fee repricing, frozen review/PSBT export, locked hardened-keypool fallback | Multi-node send/receive and encrypted restart journeys |
| 7. PSBT and replacement | `psbt` | Import/review/export/sign/broadcast PSBT, foreign PSBT restrictions, fee-bump confirmation/replacement | Complete offline and cross-wallet PSBT journeys |
| 8. Watch-only and signers | `signers` | Watch-only creation, invalid keys, unsigned transaction/export restrictions | External signer subprocess interaction, device errors and unavailable devices |
| 9. Payment request import | `payment_import` | Clipboard conflicts, draft preservation, malformed URI rejection, path/file-URL import, native paste and text/URL drops | OS command-line/open-file entry points and argument-injection checks |

Stages 1–3 run in both wallet modes; stages 4–9 run with wallets enabled. The
table describes test coverage, not implementation completeness. A stage label
does not demonstrate that the application can be compiled with later-stage code
removed. Staging PRs still need their own build and dependency checks.

Put cross-stage scenarios in the latest stage whose behavior they exercise:
watch-only PSBT export belongs to stage 8 and imported-payment review belongs to
stage 9. Earlier stages should not depend on helpers that require a loaded wallet.
Keep shared process/UI helpers in `fixture/test_util.h` and wallet setup helpers
in `fixture/test_wallet.h`. RPC is useful for arranging chain/wallet state and
checking results; exercise the UI or public model operation under test directly.
The nine CTest entries retain function/data-row isolation and may run in parallel.
Directories, CTest entries and report filenames use descriptive group names, such
as `node_setup`, `bitcoinqml_integration_node_setup` and
`integration-node_setup.xml`. The runner's `--stage=1..9` option selects the
corresponding ownership group in the table above.

## Functional-test migration

These Python scripts are superseded by integration coverage and removed from the
functional workflow:

| Former script (`test/functional/`) | Integration module |
| --- | --- |
| `qml_test_blockclock.py` | `blockclock` |
| `qml_test_blockclock_history.py` (live notifications) | `blockclock`; process restart remains in `interface_qml_node.py` |
| `qml_test_external_link_confirm.py` | `settings`; pre-init engine wiring remains in `interface_qml_preinit_onboarding.py` |
| `qml_test_blocksonly_settings.py` | `blocksonly` |
| `qml_test_console.py` | `console` |
| `qml_test_debug_log.py` | `debug-log` (live updates, pagination, file logging disabled, custom log path) |
| `qml_test_node_runtime_dialogs.py` | `node` (real Core UI signals) |
| `qml_test_create_wallet_name.py` | `wallet` |
| `qml_test_address_label_sync.py` | `receive` |
| `qml_test_activity_filter_export.py` | `activity` |
| `qml_test_custom_fee.py` | `send` |
| `qml_test_watchonly_wallet.py` | `watchonly`, `wallet` |

Component tests remain the owner of mock-based layout edge cases, such as debug
log scrolling, and wallet-creation flow variants. Integration tests cover their
connection to live models. The remaining Python tests still cover longer journeys
and OS/process boundaries: restarts, onboarding, other nodes, legacy binaries,
external signers, native dialogs, command-line imports, shutdown and tray behavior.
Keep those journeys short: exercise feature smoke and happy paths, and put detailed
validation, state combinations and error matrices in integration or component
tests. Add a bridge property only when a remaining process boundary requires it.

### Audit of Python additions on the rebased branch

The audit compares the original reviewed base
`6cdad5344018f35e8c33c77a23f6ba996d507549` with the rebased `qt6` base
`6e54a48ab571cb46b813eb1d98a301401089192f`. Both newly added GUI Python
scripts have their runtime assertions owned by stage-two integration tests:

| New or changed Python coverage | Owner and retained boundary |
| --- | --- |
| Block-clock history | `historyTracksEqualTimestampsDisconnectsAndReplacementBlocks` checks past blocks, duplicate timestamps, disconnects, replacement blocks, and model-to-dial propagation in both wallet modes. The reduced Python restart test checks restoration from disk without a new tip. |
| External-link confirmation | `aboutLinkConfirmationCancelsAndReopensWithoutNavigation` checks Website/Source destinations, confirmation controls, popup reuse, cancellation, retained About navigation, and no URL launch in both wallet modes. The existing pre-init onboarding journey keeps a single link/cancel smoke check in its separate engine. QML component tests retain open/failure/copy/scheme cases. |
| Custom fee and debug-log additions | `send` preserves the newer bounded fee assertion against actual signed size (up to two vbytes of slack), as well as the conservative size estimate. `debug-log` covers disabled file logging and custom log paths. |
| External signer | Keep subprocess/device-failure, review-input visibility, restart, and process-exit assertions in the existing signer journey. They depend on the external signer and process lifecycle. |
| Onboarding, pre-init onboarding, peers, shared harness | Changes add synchronization helpers or waits to existing multi-process/multi-node journeys, rather than new standalone runtime tests. Keep those journeys in Python. |

New runtime UI/model assertions should extend these integration modules. A
Python remainder must identify the process, startup, external-device, or
multi-node behavior that requires it; sharing a component with an integration
test alone does not justify removing that boundary coverage.

### Activity migration

The `activity` module in `wallet_activity/test_activity.cpp` replaces the
assertions from `qml_test_activity_filter_export.py` with seven independent
cases. Each case starts a fresh application, regtest node and wallet; cases that
need history create their own payment request and mine their own transaction.
RPC only arranges chain state. Filters, export and details use the live QML UI
and its production models.

| Case | Migrated assertions |
| --- | --- |
| `emptyWalletShowsSourceEmptyState` | Empty source count and the idle/syncing title and description |
| `searchAndTypeFiltersExportOnlyMatchingLiveRows` | Address/label search, Today, Mined and Payment request filters, multiple selections, BTC CSV header, immature status/amount and excluded request rows |
| `displayUnitFlowsFromSettingsToExportAndDetails` | Settings navigation preserves filters/search; sat CSV header, request note/address/amount, excluded mined rows, txid search and the selected transaction's detail amount |
| `clearingFiltersPreservesSearchAndRestoresRows` | Filtered-empty title/description, clearing filters independently of search, All/DateAll reset and restoring both rows |
| `amountRangeFiltersLiveRowsAndCsv` | Minimum amount excludes the small request from both the list and CSV; clearing restores both rows |
| `calendarRangeFiltersAndResetsLiveRows` | Calendar selection, applying a past custom range, excluding today's rows and resetting the range |
| `fulfilledRequestBecomesAnAssociatedTransactionWithoutDuplicates` | Live request fulfillment, association under the Payment request filter, transaction-only CSV with its private note/address and no duplicate pending row |

This script did not test a restart, another node, or an OS entry point. The
existing receive, send/receive, wallet compatibility and native-file-dialog
functional journeys remain responsible for those boundaries. Activity layout,
pointer/keyboard behavior and model edge cases also retain their QML and C++
unit coverage.

## Functional scenarios by stage

The functional workflow contains 11 feature suites. Scenario metadata identifies
where GUI behavior is introduced; the numbers remain selection/ownership data,
not filenames. All scripts support `--list`, `--case` and `--stage`. Individual
scenarios own their profiles and are independently runnable. Stage selection is
exact. To validate a cumulative stage, pass `--stage` for each earlier stage to
functional scripts (the option may be repeated), or run each earlier stage's
integration CTest entry. The integration runner accepts `--stage` only once.
This mapping does not certify that intermediate-stage builds are already ordered.

| Suite (`test/functional/`) | Stage | Named scenarios and retained boundary |
| --- | --- | --- |
| `interface_qml_startup.py` | 1, 2, 4 | Bridge protocol (1); onboarding, blocked-config finish/quit/close, CLI/config/JSON wallet disabling, GUI resets (2); wallet-enabled CLI override (4) |
| `interface_qml_preinit_onboarding.py` | 2, 4 | Node onboarding, existing-profile onboarding, configuration source and saved settings (2); wallet onboarding, root wallet and explicit walletdir discovery (4) |
| `interface_qml_node.py` | 2, 4 | Settings and block history share one node-only restart (2); saved geometry during wallet-shell construction (4) |
| `interface_qml_peers.py` | 3 | Multi-node disconnect/ban/unban, per-peer isolation and ban persistence across restart |
| `interface_qml_wallet.py` | 4 | Encrypted creation, backup, close/reload across restart and successful restoration |
| `interface_qml_wallet_compat.py` | 4, 5 | A single legacy fixture session covers backup refusal plus plain/encrypted migration (4); real Qt Widgets release loading QML payment-request metadata (5) |
| `interface_qml_wallet_activity.py` | 5 | Signing and address browsing share a receiver wallet; request/QR export, payment, actual native/fallback CSV dialog and receipt restoration share a restart |
| `interface_qml_wallet_sending.py` | 6, 7 | Encrypted-wallet restart and multi-node/two-recipient delivery share one journey (6); foreign signed PSBT and encrypted RBF (7) |
| `interface_qml_external_signer.py` | 8 | External signer subprocesses, device failures, review and restart |
| `interface_qml_payment_import.py` | 9 | Clipboard/manual/file happy paths and command-line argument-injection security |
| `interface_qml_shutdown.py` | 1, 2, 4 | Nine fatal initialized-node exits (1); onboarding close/native Quit (2); close/native Quit/SIGINT while real startup wallets load (4) |

For a cross-feature journey, assign the latest GUI feature it exercises. Core
RPC funding is setup and does not introduce GUI Send into an Activity scenario.
Wallet-loading shutdown requires stage 4 even though generic lifetime belongs to
stage 1. Detailed runtime matrices remain in the existing integration modules.
Shared helpers and the CTest-only configured-datadir/fatal-unit drivers are not
additional feature suites. CTest selects `fatal-*` from the shutdown suite.

### Consolidation and coverage ownership

The original 29-script workflow was reduced to 24 by moving detailed assertions
into integration tests. This pass combines those 24 entry points into the 11
suites above and removes repeated setup and restart work:

| Former coverage | Current owner |
| --- | --- |
| Wallet settings/selector/create/password/backup/restore | `wallet` integration owns validation, row targeting, context changes and errors. Wallet process smoke retains backup/reload/restore; encrypted send uses the stage-6 journey. |
| Proxy/display/tray/geometry/history/reset | `settings`, `navigation`, `blockclock` integration own live validation and state combinations. Node smoke shares settings/history persistence; startup retains before/after-config reset regressions. |
| Addresses/signing/receive/Activity/QR/file dialog | `receive` and `activity` integration and QML components own detailed fields, filters, metadata, receipt totals and replacement handling. Activity smoke retains signing, a real payment, saved QR, actual dialog implementation and persisted receipt. |
| Send/maximum/coin selection/custom fee | `send` integration and QML components own fee policy, maximum/sweep/ownership matrices, locks, draft/review preservation and hardened-keypool fallback. Sending smoke retains encrypted restart and actual multi-node delivery. |
| PSBT and RBF | `psbt` integration/QML own detailed review and signing restrictions; Core owns confirmed-transaction RPC rejection. Stage-7 scenarios retain cross-wallet signed PSBT and encrypted GUI replacement. |
| Legacy restore/migration/Qt compatibility | Compatibility suite retains real old-daemon wallets and backups, encrypted migration and a real Qt Widgets process. Invalid backups and ordinary restore details stay in `wallet` integration. |
| URI import and arguments | `payment-import` integration owns malformed/conflicting imports, native paste and actual drop events. Stage-9 scenarios retain import happy paths and process argument security. |
| Bridge/onboarding/peers/signers/shutdown | Named process-boundary scenarios retain their existing checks; detailed live-model checks remain in their integration modules. |

Use `python3 test/functional/interface_qml_wallet_sending.py --stage 7` to run
only PSBT/replacement, or `--case signed-foreign-psbt` to select one journey.
See [test bridge usage](test-bridge.md#running-tests) for selection and diagnostics.

## Sanitizers

Reuse the existing build directory, generator and compiler cache. For example:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_APP_TESTS=ON -DWITH_CCACHE=ON -DENABLE_IPC=OFF \
  -DSANITIZERS=address,undefined,integer,implicit-conversion,local-bounds,nullability,float-divide-by-zero
cmake --build build --target bitcoinqml_integration_tests -j12
cat bitcoin/test/sanitizer_suppressions/ubsan test/sanitizer_suppressions/ubsan > build/ubsan.supp
```

Run with `ASAN_OPTIONS=halt_on_error=1:detect_stack_use_after_return=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:report_error_type=1:suppressions=PATH`,
where `PATH` is the absolute path to `build/ubsan.supp`. Core's file documents
intentional integer wrapping in cryptography and dependencies; the GUI additions
cover Qt's deliberate unsigned hash arithmetic. Pass these environment values
with `cmake -E env`, followed by `ctest --test-dir build -L integration -j3
--output-on-failure`. The parent passes the environment to discovery and test
children, so sanitizer failures fail their rows and retain diagnostics.

ThreadSanitizer requires a separate build configuration with `-DSANITIZERS=thread`;
it cannot run with AddressSanitizer. When switching sanitizer configurations with
Makefiles, run `cmake --build build --target clean` before rebuilding: secp256k1
appends sanitizer flags to its C compile rule, and stale objects can retain the
previous instrumentation. This preserves the build configuration and compiler cache. System Qt libraries may be uninstrumented,
which limits concurrency coverage and can produce reports around Qt synchronization.
Inspect each report before introducing a narrowly justified dependency suppression.
Never suppress an entire application module merely to make the run pass.

Availability is compiler/platform dependent: Apple Clang on macOS arm64 does not
support standalone LeakSanitizer, MemorySanitizer or HWAddressSanitizer. MemorySanitizer
also requires instrumented dependencies on supported platforms. Consult
[Clang's sanitizer documentation](https://clang.llvm.org/docs/UsersManual.html#controlling-code-generation)
before adding flags. These runs only exercise the selected integration scenarios;
they do not establish the absence of memory or concurrency defects.

Save reports from `build/test-reports/` and failed profiles before changing
configurations. Restore the regular build with `-DSANITIZERS=` and rebuild the
targets you use. Live debug-log tests require OS file-change notifications; a
restricted environment that blocks them cannot validate live updates.
