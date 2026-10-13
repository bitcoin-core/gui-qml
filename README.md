# Bitcoin Core App

*(The QML GUI)*

**WARNING: THIS IS EXPERIMENTAL, DO NOT USE BUILDS FROM THIS REPO FOR REAL TRANSACTIONS!**

This directory contains the source code for an experimental Bitcoin Core graphical user interface (GUI) built using the [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html) framework.

Unsecure CI artifacts are available for local testing of the master branch, avoiding the need to build. These can be found under the [Actions](https://github.com/bitcoin-core/gui-qml/actions?query=branch%3Aqt6) tab. It is required to have and be logged into a github account in order to download these.

Note: For macOS, the CI artifact binary must be made executable and code-signed before it can
be ran. To make executable and apply a signature, run the following on the unzipped CI artifact:

```
chmod +x ./Downloads/bitcoin-core-app && codesign -s - ./Downloads/bitcoin-core-app
```

## Goals and Limitations

The current Bitcoin Core GUI has gathered enough technical debt and hacked on features; it is time to begin anew.
This project will start from a clean slate to produce a feature-rich GUI with intuitive user flows and first-class design.

The primary goals of the project can be summed up as follows:

- Implement UX/UI best-practices as documented in the [Bitcoin Design Guide](https://bitcoin.design/guide/)
- Engage with the Bitcoin Design community to implement well-designed features
- Work alongside the Bitcoin Design community to develop an aesthetic GUI
- Develop a mobile-optimized GUI

Avoid conflicts with the Bitcoin Core repository by importing it unmodified as a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules).
As such, this project **can not** accept pull requests making any significant changes unrelated to the GUI.
Pull requests must be focused on developing the GUI itself, adding build support, or improving relevant documentation.


## Development Process

This repo is synced with the [Bitcoin Core repo](https://github.com/bitcoin/bitcoin) on a regular basis.

Contributions are welcome from all, developers and designers. If you are a new contributor, please read [CONTRIBUTING.md](https://github.com/bitcoin/bitcoin/blob/master/CONTRIBUTING.md).

### Minimum Required Qt Version

All development must adhere to the current upstream Qt Version to minimize our divergence from upstream and avoid costly changes. Review of open PR's must ensure that changes are compatible with this Qt version. Currently, the required version is [Qt 6.2](https://github.com/bitcoin/bitcoin/blob/master/doc/dependencies.md#build-1).

As the Qt Version changes upstream, refactoring is allowed to use the newly available features.

### Policies

This project has custom policies for development, see:
- [Icon Policy](./doc/icon-policy.md)
- [Translator Comments Policy](./doc/translator-comments.md)

## Compile and Run

The master branch is only guaranteed to work and build on Debian-based systems and macOS.
Support for more systems will be confirmed and documented as the project matures.

### Dependencies

Bitcoin Core App requires all the same dependencies as Bitcoin Core, see the
appropriate document for your platform:

- [build-osx.md](https://github.com/bitcoin/bitcoin/blob/master/doc/build-osx.md)

- [build-unix.md](https://github.com/bitcoin/bitcoin/blob/master/doc/build-unix.md)

In addition the following dependencies are required for the GUI and tests:

#### Debian-based systems:

```
sudo apt install \
  qt6-base-dev \
  qt6-base-dev-tools \
  qt6-tools-dev \
  qt6-l10n-tools \
  qt6-tools-dev-tools \
  qt6-declarative-dev \
  qt6-svg-dev \
  qml6-module-qt-labs-platform \
  qml6-module-qt-labs-settings \
  qml6-module-qtquick \
  qml6-module-qtquick-controls \
  qml6-module-qtquick-dialogs \
  qml6-module-qtquick-shapes qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates \
  qml6-module-qtquick-window \
  qml6-module-qtqml-models qml6-module-qtqml \
  qml6-module-qtqml-workerscript \
  libgl-dev \
  libqrencode-dev
```

Additionally, to support Wayland protocol for modern desktop environments:

```
sudo apt install qt6-wayland
```

#### macOS:

```
brew install qt@6 qrencode qtdeclarative
# Help CMake find Qt6 on macOS (adjust if needed)
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)/lib/cmake"
```

### Build

1. Install the required dependencies for your platform and clone the repository
2. Fetch the Bitcoin Core submodule:
```
git submodule update --init
```
3. Configure
```
cmake -B build
```
4. Build
```
cmake --build build -j$(nproc)
```

### Run

Binaries are exported to the `build/` directory:
```
build/bin/bitcoin-core-app
```

### Run Tests

Enable tests with `-DBUILD_APP_TESTS=ON`. The test directories separate the kinds of coverage:

- `test/test_*.cpp`: unit tests for models, controllers, and helpers. They do not start a full node.
- `test/qml/`: QML component tests with mock models.
- `test/integration/`: C++ tests that run the production application and a real regtest node, with their own CMake file.
- `test/functional/`: Python tests that drive the application through the test bridge.

Build and run the unit and QML component tests:

```sh
cmake -B build -DBUILD_APP_TESTS=ON
cmake --build build --target bitcoinqml_unit_tests bitcoinqml_bootstrap_tests bitcoinqml_qmltests -j$(nproc)
ctest --test-dir build --output-on-failure -L 'unit|qml'
```

Build and run the application and node integration tests:

```sh
cmake -B build -DBUILD_APP_TESTS=ON
cmake --build build --target bitcoinqml_integration -j$(nproc)
ctest --test-dir build --output-on-failure -L integration
```

The integration target builds `bitcoinqml_integration_tests`. Its runner starts a
fresh process for each module and supported wallet mode. Each child uses production
application startup and shutdown. The initial modules are `lifecycle`, `navigation`,
and `node`, each run with wallets disabled and enabled.
Each process has its own temporary data directory and GUI settings, so changes to
wallets, the chain, settings, and UI state cannot affect the next module.
The runner prints each process's output when it finishes and fails if a process
cannot start or exits unsuccessfully. Select modules and modes with:

```sh
build/bin/bitcoinqml_integration_tests --list-modules
QT_QPA_PLATFORM=minimal QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 build/bin/bitcoinqml_integration_tests --module=navigation
QT_QPA_PLATFORM=minimal QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 build/bin/bitcoinqml_integration_tests --module=node --wallet=enabled
```

Omitting `--module` selects all modules; omitting `--wallet` selects each module's
supported modes. Qt Test arguments are forwarded to the selected module. For
example, add `chainSyncFollowsRealCoreBlockNotifications` to the last command to
run that individual test with a fresh node and profile.

`TestApplication` extends the production `BitcoinQmlApplication` and provides
profile isolation, node and wallet initialization waits, test scheduling, and
shutdown cleanup for the test modules. Wallet-enabled runs start without loaded
wallets; wallet test modules can create the wallets they need.
Test cases live in separate modules and take a `BitcoinQmlApplication&`, giving
them access to the same node, models, and QML engine as the application.
`test/integration/runner/` contains the entry point, process runner, and module
declarations. `test/integration/fixture/` contains the application fixture and
shared test helpers. Test case modules live directly in `test/integration/`.
Add a new module to `test/integration/CMakeLists.txt`, declare its runner in
`runner/test_cases.h`, and add its name, supported wallet modes, and runner to
`IntegrationTestModules()` in `runner/test_cases.cpp`. Use the runner signature
`int RunFeatureTests(BitcoinQmlApplication&, WalletMode, int argc, char* argv[])`
and pass `argc`/`argv` to `QTest::qExec`.

Test methods within a module share state. Each independent case should create its
own wallets and restore settings/navigation in `init()`/`cleanup()`, including on
assertion failure. Cases that need a fresh chain or conflicting startup options
should be separate modules or separate filtered invocations. Shared state is
appropriate for the ordered startup-to-shutdown checks in the lifecycle module.
The fixture always requests shutdown and verifies UI teardown when a module ends.

To run everything, build the default target and run CTest without a label filter.

- If CMake reports "Target Qt6::Test not found", ensure Qt6 Test is available
  and discoverable by CMake:

  - Debian/Ubuntu: `qt6-base-dev` provides the Qt Test module (already listed above).
  - macOS: set `CMAKE_PREFIX_PATH` as shown so CMake can locate Qt 6 modules.
