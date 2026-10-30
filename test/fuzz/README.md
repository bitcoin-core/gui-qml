# Fuzzing Framework

This directory contains fuzzing targets and tooling for the QML application's
C++ backend. Fuzz targets are built with libFuzzer and run under AddressSanitizer
and UndefinedBehaviorSanitizer.

## Building

On macOS (Homebrew):

```sh
./test/fuzz/build.sh
```

Or manually:

```sh
cmake -S . -B build-fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_FUZZ_TESTS=ON \
  -DBUILD_APP_TESTS=OFF
cmake --build build-fuzz --parallel
```

On Ubuntu CI, see `.github/workflows/fuzz.yml`.

## Running

Run every built target for a short time (default 30 s each):

```sh
./test/fuzz/run.sh
```

Override the per-target duration:

```sh
./test/fuzz/run.sh 60
```

Run one target manually:

```sh
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/test/fuzz/fuzz_bitcoin_uri corpus/ -max_total_time=60
```

## Static analysis

```sh
python3 test/fuzz/qml_security_lint.py qml
```

## Targets

### Parsing / formatting

- `fuzz_bech32` — Bech32/Bech32m decoding
- `fuzz_bitcoin_address` / `fuzz_bitcoin_address_format` — address validation and formatting
- `fuzz_bitcoin_amount` / `fuzz_bitcoin_amount_format` — amount parsing/formatting
- `fuzz_bitcoin_units` / `fuzz_qml_bitcoinunits_extra` — unit labels and conversions
- `fuzz_bitcoin_uri` / `fuzz_bitcoin_uri_model` — `bitcoin:` URI parsing
- `fuzz_qml_util` — `QmlUtil` helpers
- `fuzz_qml_datadir` — data-dir path normalization and validation

### Models

- `fuzz_activityfilter` — `ActivityFilterProxyModel` date/search/amount filters
- `fuzz_address_list_model` — `AddressListModel` category/filter state
- `fuzz_ban_list_model` — `BanListModel` refresh and unban
- `fuzz_block_clock_model` — `BlockClockModel` time-fraction and history loading
- `fuzz_chain_model` — `ChainModel` network-name metadata
- `fuzz_coins_list_model` — `CoinsListModel` search/sort/filter/group state
- `fuzz_debug_log_model` — `DebugLogModel` log parsing and filtering
- `fuzz_network_status_model` — `NetworkStatusModel` reachability handling
- `fuzz_nodemodel` / `fuzz_nodemodel_info` — `NodeModel` proxy/peer helpers
- `fuzz_onboarding_options_model` / `fuzz_onboarding_startup` / `fuzz_onboarding_storage` — onboarding settings
- `fuzz_options_model` — `OptionsQmlModel` settings validation
- `fuzz_payment_request` — `PaymentRequest` URI builder and property setters
- `fuzz_peer_list_model` / `fuzz_peer_list_sort_proxy` — peer table and sort/filter proxy
- `fuzz_psbtqmlmodel` — PSBT model handling
- `fuzz_rpc_command_executor` / `fuzz_rpc_console_model` — RPC command parsing and console
- `fuzz_send_recipient` — `SendRecipient` address/amount validation
- `fuzz_sign_verify_message_model` — `SignVerifyMessageModel` verify path
- `fuzz_syncprogresstracker` — sync progress tracking
- `fuzz_transaction` — `Transaction` model formatting
- `fuzz_walletqmlmodel` — wallet model string helpers

### Graphics / media

- `fuzz_image` — `QImage` loading and format conversion
- `fuzz_qrimageprovider` — QR image provider
- `fuzz_svg` — SVG loading

## Coverage notes

These targets exercise the C++ backend methods that are reachable from QML.
They do **not** fuzz the QML engine itself, the JavaScript runtime, translation
files (`.qm`), settings files (`.ini`), font files, or the full command-line
argument parser.

## Skipped targets

The following surface is currently **not** covered, either because it requires a
real wallet/node backend, has heavy GUI dependencies, or is better handled by
upstream Bitcoin Core fuzzers:

- `TransactionActivityModel` / `TransactionActivity` — needs a loaded wallet.
- `WalletQmlModelTransaction` — needs wallet state.
- `WalletListModel` — needs `interfaces::Node` wallet loader mocks.
- `BumpTransactionModel` fee-bump confirmation path — needs a real wallet.
- `ReceiveRequestHistoryModel` persistent storage — needs wallet backend.
- `CoinsListModel` coin locking/selection — needs wallet backend.
- `CoreSettingsModel` runtime mutation — mostly exercised through
  `fuzz_options_model` and `fuzz_onboarding_options_model`.
- `OptionsQmlModel` numeric settings (`dbcache`, `par`, `maxmempool`) and
  restart-required transitions — could be added, but the string validation path
  is already covered.
- `PaymentRequest` BIP70 / payment-protocol file handling — not used by the QML
  UI in this codebase.
- `QML engine` / `QQmlEngine::loadData` with mutated QML — high-value but needs
  a full application context; left for future work.
- `QSettings` file parsing and legacy settings migration — partially touched via
  options/onboarding models, but not targeted directly.
- `NetworkTrafficTower`, `DebugLogModel` async worker completion, and other
  timer-driven paths — timers are disabled or not waited on to keep fuzz rounds
  fast.

## Future targets

Good next additions once the missing dependencies are mocked or the payoff
justifies the complexity:

1. Stateful options/onboarding fuzzer — sequences of `setProxyAddress`,
   `setDataDir`, `commitProxyLocation`, etc.
2. `TransactionActivityModel` with a stub wallet returning canned transactions.
3. `WalletQmlModelTransaction` amount/fee/total formatting.
4. `WalletListModel` with mocked `interfaces::WalletLoader`.
5. `QmlEngine` fuzzer that feeds mutated `.qml` snippets to
   `QQmlEngine::loadData`.
6. Settings-file fuzzer that writes malformed `.ini` data and constructs
   `OptionsQmlModel` / `OnboardingOptionsModel`.
7. `DesktopWindowBehaviorModel` / `DesktopTrayIconController` settings paths.
8. `NetworkTrafficTower` with crafted traffic samples.
