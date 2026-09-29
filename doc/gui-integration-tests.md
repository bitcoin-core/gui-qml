# GUI integration tests

Build and run the first migration chunk:

```sh
cmake -S . -B build -DBUILD_APP_TESTS=ON -DENABLE_IPC=ON
cmake --build build --target bitcoinqml_integration_tests bitcoinqml_startup_tests -j4
ctest --test-dir build -R '^bitcoinqml_(integration_tests|startup_tests)$' --output-on-failure
```

CTest supplies the minimal Qt platform, software rendering, a 120-second timeout,
and disables the QML disk cache. The test automation bridge is not required.
The ordinary GUI test configuration also discovers this test, so the existing
Qt 6.2/6.4/6.10 CI matrix builds and runs it.

## First chunk: node shell and onboarding

This ports the application integration harness introduced by staging commit
`7e060eaf57fb046aaeca3565bd3101d98d5ae0bd`, and the feature registry and
onboarding/node/navigation cases from chunk 2 at
`8945e933061023aaba669fecea02f26d92d2ff3f`, to the `qt6` application structure.
The onboarding cases originate in `747485d9831f51f3a16e7b856fb50d5f895ae69f`.
These source contributions are by johnny9 and Jarol Rodriguez, with the
coauthors credited in the original commits. The port does not depend on the
staged model decomposition or C++ router.

There is one QApplication and one real Core lifecycle per process. A small
observer at the production entry point gives the fixture access to the real
node and QML engine before event processing begins. The fixture waits for the
production initialization signal, runs independently registered feature tests,
then requests normal shutdown, including after assertion failures. The process
must return successfully after the engine is destroyed and worker threads join.

Each invocation has temporary Core and Qt settings directories. Listening,
discovery, automatic outbound connections, DNS seeds, and NAT mapping are
disabled. Node cases restore network state in cleanup. Startup cases launch
separate application processes using isolated profiles. GUI actions use mouse events on production controls; Core RPC
creates a block to test incoming notifications and independently checks state.

| Source coverage | In-process replacement | Python disposition |
| --- | --- | --- |
| Staging application lifecycle | Real regtest startup, translations, node-only shell, shutdown page, idempotent shutdown, successful teardown | Keep executable startup and separate-process shutdown checks |
| Chunk 2 node integration | Network model actions agree with Core; external Core network changes reach the model; a mined block reaches the real model via notifications | No duplicate Python case added |
| `qml_test_blockclock.py` | Click pause/resume; verify all original state/header/subtext assertions plus independent Core network state | Replaced; removed from the functional workflow |
| Chunk 2 navigation | Click settings, visit display/window/storage/connection/about, wait for rendering, reject QML warnings, return to the retained node page | Does not replace settings persistence or desktop/native behavior tests |
| Chunk 2 onboarding | Existing-profile read-only preview, command-line precedence, settings/config attribution, invalid/valid proxy drafts | Partial coverage of `qml_test_preinit_onboarding.py`; retain its process/configuration permutations |
| `qml_test_onboarding.py` page progression | Click through all six production pre-init pages, apply choices, start the real node, then restart the saved profile | Retain wallet routing and the remaining Python permutations |

This is a useful first chunk because it establishes the real application fixture
and covers frequent node interactions without mining wallet funds or depending
on historical binaries. It does not remove Python tests merely because their
pages load in C++.

## Production startup scenarios

`bitcoinqml_startup_tests` launches a fresh child process per startup. Each child
calls `QmlGuiMain` with real command-line arguments, including a data-directory
path containing spaces and a non-ASCII character. Production, the audited Python
executable, and application tests share `noui_connect` inside
`RunQmlApplication`; native Windows argument normalization remains in
`QmlGuiMain` and is also used by the startup children.

`QmlApplicationHooks` exposes the actual pre-init window immediately before its
event loop, the newly created interfaces before model construction, and the main
window before its event loop. Tests queue actions into those loops. They do not
construct a substitute onboarding engine, call initialization themselves, or
inject `-qml_onboarded=1` into the first-run/restart journey.

The production onboarding window now has an explicit initial size matching its
minimum size. The real-window test exposed that relying on a native minimum-size
resize left controls with zero size under the minimal platform.

The scenarios cover:

- First run: choose reduced storage through the real controls, finish onboarding,
  check the main-window handoff, and verify the real node reports pruning enabled.
- Restart: reuse the files written by the first process, require onboarding to be
  skipped, and verify the saved pruning choice still controls the node.
- Cancellation: close the final onboarding page, require zero interface creation
  and zero Core initialization/shutdown calls, and require no settings or block
  database to be created. A subsequent launch must still show onboarding.
- Reset: an onboarded profile with `-resetguisettings` must show onboarding again;
  completing it must produce a profile that can restart normally.
- Disabled settings: `-nosettings` starts directly and creates no settings file.
- Existing profiles: wallet-disable and pruning settings come from bitcoin.conf,
  settings.json over configuration, and command-line overrides over both. The
  assertions inspect the actual main-window models and Core RPC results.

All started nodes must initialize and shut down exactly once, and both production
QML engines must be destroyed before the child returns. The parent checks the
child exit status and captured output for thread-guard and sanitizer failures;
it kills a child that exceeds its bounded timeout. The thread guard is active
throughout interface use, including startup and shutdown.

Runtime snapshots and actions wait for `NodeModel::nodeReady`, emitted only
after successful initialization. Peer, ban, chain, RPC-completion, mempool and
traffic reads therefore cannot overlap construction of Core's runtime objects.
Warnings and initialization progress still reach the GUI while startup runs.
Regression tests cover failed initialization and a late success after draining.

These scenarios currently exercise runtime-disabled-wallet startup. They do not
certify wallet discovery/creation or remote IPC startup, and the minimal platform
does not certify native window-system behavior. The explicit isolated data
directory also leaves OS default-directory discovery to the existing process
tests. Native argument code is shared, but a Linux run is not Windows validation.

The sanitizer startup run exposed a pre-existing narrowing of the sync timer's
millisecond epoch timestamp into `int`. The port keeps timestamps and elapsed
times in `qint64` and clamps the remaining-time estimate before converting to
the public `int` property. Startup and real block notifications cover this path.
Sanitizer CTest runs combine the pinned Core UBSan suppressions with staging's
existing suppression for Qt 6.4's intentional unsigned hash overflow. Application
integer checks remain enabled. The narrow Qt rendering leak suppression is
described below.

## GUI thread enforcement

`bitcoinqml_integration_tests` and `bitcoinqml_audited_app` replace the Node and
Chain handles immediately after interface creation, before base initialization
or model construction. This is an injection seam around the existing Core
interfaces; models do not need a second provider API.

The checked Node returns a stable checked WalletLoader. Wallet creation, loading,
restoration, migration, enumeration, and load notifications all return checked
wallets. External signer handles are also checked. Wrappers forward ownership,
results, errors, callbacks and handler lifetimes. Raw `context()`/`wallet()`
implementation access is rejected on every thread because it would bypass the
check. The audit owns no application state and is shared until its last wrapper
and outstanding callback are destroyed.

`test/thread_policy.json` classifies every method, including inherited default
virtual methods. `bitcoinqml_thread_policy` parses the pinned headers and checks
the forwarding code; interface additions or signature changes fail until the
policy is reviewed and `test/generate_thread_audit.py --write` is run. The default
runtime policy requires a worker, including getters and try-lock calls.

There are three narrowly scoped exceptions for the current monolithic backend:

- `baseInitialize`, initial persistent-setting reads and the explicitly listed
  Node signal subscriptions run during bootstrap, before the runtime window.
  Those same calls on the GUI thread fail once bootstrap ends.
- `shutdownRequested` reads the local atomic shutdown flag.
- `Node::walletLoader` obtains the stable local loader and returns only its
  checked wrapper. Loader operations still require a worker.

These exceptions are disabled when the audit is constructed for a remote proxy.
`startShutdown` is **not** an exception: it interrupts services and can acquire
locks or execute a shutdown command. A separate shutdown worker runs it so a
blocked feature worker cannot prevent cancellation.

The guard fails unconditionally before forwarding, including in Release builds.
Diagnostics name the interface method and lifecycle phase and never include call
arguments. Fixture RPCs use checked handles on a separate worker while the GUI
continues processing events. Negative subprocess tests verify actual process
failure, the expected diagnostic (even with a custom Qt message handler) and
rejection before backend entry. Positive
tests verify forwarding, stable loader identity, all wallet delivery routes and
ownership after the loader and original audit owner are destroyed.

The first application chunk moves node status/peer/ban reads and mutations,
block-clock history and RPC completion-list loading off the GUI thread. QML reads
cached state, and result application asserts model thread affinity. Snapshot
requests coalesce instead of accumulating. Accepted peer actions report their
asynchronous result to the UI. Shutdown closes admission, rejects late results,
and drains workers before Core destruction, with the shutdown page still active.
Network-traffic sampling starts only after successful Core initialization. Merely
constructing the model or opening the traffic page must not read the connection
manager while Core is creating it. Shutdown prevents a late initialization
result from restarting sampling.

A bounded watchdog releases deliberately blocked backend calls even if the GUI
stalls. Tests require GUI event delivery **before** release, exercise navigation
while a real backend read is held, and shut down with both a held read and an
unbounded `waitfornewblock` RPC pending. Notification tests verify coalescing and
that shutdown discards a pending snapshot. These are event-order assertions, not
frame-rate thresholds.

For an existing Python journey, build with `-DENABLE_TEST_AUTOMATION=ON` and use:

```sh
BITCOIN_CORE_APP="$PWD/build/bin/bitcoinqml_audited_app" \
QML_TEST_CHECK_EXIT=1 QT_QPA_PLATFORM=minimal QT_QUICK_BACKEND=software \
python3 test/functional/qml_test_disablewallet_boot.py --node-only
```

With automation enabled, CTest registers this as `bitcoinqml_audited_python` and
the functional workflow runs it alongside the integration and guard tests. The
node-only selection leaves the wallet-enabled override case in the original
Python suite. The opt-in process check rejects abnormal shutdown, audit failures and sanitizer
diagnostics. Other Python journeys can use the same executable as their paths
are migrated; the strict audit does not silently exempt them.

### Boundaries of this first chunk

The enforced application journeys deliberately use `-disablewallet`. Wallet
wrapping and delivery are tested, but the existing Qt6 wallet UI still contains
synchronous operations and raw Core wallet access that the audit will reject.
Wallet overview/security/catalog snapshots, delayed wallet selection/unload,
migration inspection, and transaction actions need their own application
migration and real-wallet journeys before replacing those Python tests. This
introduction does not certify those paths or mark the entire wallet refactor
complete.

`ENABLE_IPC=ON` compiles the dependency support, but this Qt6 application's
`MakeGuiInit` is the monolithic Qt implementation. The remote-policy unit test
proves local exceptions are unavailable to proxies; it is not a real IPC journey.
Actual client-side IPC validation remains a prerequisite for claiming transport
coverage. Direct filesystem access, QML computation and other uninstrumented
work can also stall the GUI; passing these journeys does not prove otherwise.

### Qt rendering leak checks

For leak checks with Qt older than 6.6, the QML rendering test adds one narrow
suppression for `QSGRenderContext::textureForFactory`. Qt 6.4's software render
context does not free its texture cache during invalidation; Qt 6.6 does. A
standalone Qt Quick Image reproduces the leak, while an unrelated application
allocation still fails with the suppression enabled. This suppression is not
applied to production or audited process-lifecycle tests, or to newer Qt versions.
The transaction-flow component separately releases its Instantiator references
before Shape deletes the generated paths; those application leaks are fixed.

## Next chunks

1. Port the runtime dialog message/button matrix through Core notifications,
   and runtime proxy drafts, discard/save, and on-disk persistence. Replace
   `qml_test_node_runtime_dialogs.py` and `qml_test_proxy.py` only after all their
   assertions have equivalents.
2. Port display and blocksonly settings assertions; run startup/reset/config
   permutations in separate integration CTest processes. Keep native window,
   tray, URI delivery and process restart checks where that boundary matters.
3. Add isolated real descriptor-wallet fixtures, then move wallet creation,
   selection and close/reload coverage before send/receive/PSBT matrices.

Retain a short node and wallet executable journey. Keep interoperability with
historical wallets and external signers in their designated process tests.

## Validation scope

This branch keeps Core as a pinned submodule. It has no top-level `ci/lint.py`
or `ci/test_run_all.sh`, and the pinned Core revision has no `interface_gui.py`.
Running Core's scripts alone does not check the parent QML application. Report
these limits explicitly; a supplemental application sanitizer build or targeted
clang-tidy run is not a pass of staging's complete CI jobs.
