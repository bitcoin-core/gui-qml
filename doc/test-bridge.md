# QML Test Automation Bridge

The test bridge is a lightweight IPC server embedded in `bitcoin-core-app` that
allows external test scripts to observe and drive the QML user interface. It is
designed for use with the Bitcoin Core functional test framework
(`BitcoinTestFramework`) and communicates over a Unix domain socket using
newline-delimited JSON messages.

The bridge is **test-only infrastructure** — it is compiled in only when
`ENABLE_TEST_AUTOMATION` is set at build time and activated only when the
`-test-automation` flag is passed at runtime.

## Building with the test bridge

```bash
cmake -B build -DENABLE_TEST_AUTOMATION=ON
cmake --build build
```

When `ENABLE_TEST_AUTOMATION=ON`:

- The `qml/test/` directory is included in the build.
- The `ENABLE_TEST_AUTOMATION` preprocessor macro is defined.
- `Qt6::Network` is linked (provides `QLocalServer` / `QLocalSocket`).

When `ENABLE_TEST_AUTOMATION=OFF` (the default), none of the test bridge code
is compiled into the binary.

## Running with the test bridge

```bash
# Specify a socket path explicitly
./build/bin/bitcoin-core-app -test-automation=/tmp/test_bridge.sock

# Or use the default path (<datadir>/test_bridge.sock)
./build/bin/bitcoin-core-app -test-automation
```

For headless CI environments, combine with the Qt offscreen platform:

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/bitcoin-core-app -test-automation=/tmp/test_bridge.sock
```

## Architecture

```
┌─────────────────────────────────────┐
│  Python functional test             │
│  (BitcoinTestFramework subclass)    │
│                                     │
│  ┌──────────┐    ┌───────────────┐  │
│  │ JSON-RPC │    │ QmlDriver     │  │
│  │ (backend │    │ (UI actions   │  │
│  │  state)  │    │  via socket)  │  │
│  └────┬─────┘    └──────┬────────┘  │
└───────┼─────────────────┼───────────┘
        │                 │
        ▼                 ▼
┌─────────────────────────────────────┐
│  bitcoin-core-app                   │
│                                     │
│  ┌──────────┐    ┌───────────────┐  │
│  │ RPC      │    │ TestBridge    │  │
│  │ server   │    │ (QLocalServer │  │
│  │          │    │  JSON IPC)    │  │
│  └──────────┘    └───────────────┘  │
└─────────────────────────────────────┘
```

Two channels work together in functional tests:

- **JSON-RPC** (already exists) — set up backend state: create wallets, fund
  addresses, generate blocks.
- **Test bridge socket** (new) — observe and drive the QML UI.

## Protocol

The test bridge accepts **newline-delimited JSON** commands over a Unix domain
socket and returns a single JSON response line for each command.

### Commands

#### `get_current_page`

Returns the `objectName` (or QML class name) of the current page shown in the
main `StackView`.

```json
→ {"cmd": "get_current_page"}
← {"page": "CreateWalletWizard"}
```

#### `get_property`

Reads an arbitrary property from a named QML object.

```json
→ {"cmd": "get_property", "objectName": "importButton", "prop": "visible"}
← {"value": true}
```

#### `click`

Simulates a click on a named QML object. Tests should prefer this command over
invoking control signals directly because it exercises the UI path more like a
user action. The bridge tries these strategies in order:

1. Invoke `click()` or `trigger()` if the control exposes either method.
2. Synthesize mouse press/release events at the item center.
3. As a last resort, invoke raw user-action methods/signals such as `toggle()`,
   `toggled()`, or `clicked()`.

```json
→ {"cmd": "click", "objectName": "importButton"}
← {"ok": true}
```

#### `set_text`

Sets the `text` property on a named QML object (e.g., `TextField`).

```json
→ {"cmd": "set_text", "objectName": "walletNameField", "text": "my_wallet"}
← {"ok": true}
```

#### `get_text`

Reads the `text` property from a named QML object.

```json
→ {"cmd": "get_text", "objectName": "errorLabel"}
← {"text": "File not found"}
```

#### `invoke`

Invokes a method or signal on a named QML object. Use this for explicit helper
methods or non-UI actions. Do not use it as a substitute for user interaction:
invoking a signal directly, such as `clicked()`, can bypass QML handlers or
intermediate control behavior that would run during a real click. Prefer
`click`, `set_text`, `key_click`, and other user-like commands when a test is
asserting UI behavior.

`invoke` accepts zero arguments or one string argument. Untyped QML functions
also accept two or three JSON arguments, converted to `QVariant`. For example,
the widget persistence test calls `resizeTo("blockclock", 3, 3)` after separate
Qt Quick tests verify pointer delivery and resizing.

```json
→ {"cmd": "invoke", "objectName": "testHelper", "method": "resetState"}
← {"ok": true}
```

#### `wait_for_page`

Blocks until the named QML object exists and is visible, or the timeout
expires. The bridge processes Qt events while waiting so the UI can update.

```json
→ {"cmd": "wait_for_page", "page": "ImportReview", "timeout": 5000}
← {"ok": true}
```

If the timeout is reached:

```json
← {"error": "Timed out waiting for page: ImportReview"}
```

#### `list_objects`

Returns all QML objects in the tree that have a non-empty `objectName`.
Useful for debugging and discovering available targets.

```json
→ {"cmd": "list_objects"}
← {"objects": [
     {"objectName": "main", "className": "PageStack"},
     {"objectName": "importButton", "className": "ContinueButton_QMLTYPE_42"},
     ...
   ]}
```

### Error responses

All commands may return an error response instead of their normal result:

```json
← {"error": "Object not found: someButton"}
```

## Python client — `QmlDriver`

A ready-to-use Python client is provided at `test/functional/qml_driver.py`.

```python
from qml_driver import QmlDriver

gui = QmlDriver("/tmp/test_bridge.sock")

gui.click("importWalletButton")
gui.wait_for_page("ImportWallet")
gui.set_text("walletNameField", "my_wallet")

page = gui.get_current_page()
text = gui.get_text("errorLabel")
visible = gui.get_property("importButton", "visible")
objects = gui.list_objects()

gui.close()
```

The driver retries the initial connection for up to 30 seconds (configurable
via the `timeout` constructor parameter), which allows it to connect even if
the GUI process hasn't finished starting yet.

All commands raise `QmlDriverError` on failure.

## Running tests

Test scripts live in `test/functional/`. They can either launch a fresh
headless GUI instance automatically, or attach to an already-running app.

### Launch a new instance (default)

```bash
python3 test/functional/qml_test_bridge_sanity.py
python3 test/functional/qml_test_onboarding.py
```

The harness starts `bitcoin-core-app` with `QT_QPA_PLATFORM=offscreen`,
`-resetguisettings`, and a temporary datadir. The process is shut down
automatically when the test finishes.

### Attach to a running instance

Start the app with the test bridge enabled:

```bash
./build/bin/bitcoin-core-app -test-automation=/tmp/test_bridge.sock
```

Then run tests against it:

```bash
python3 test/functional/qml_test_bridge_sanity.py --socket-path /tmp/test_bridge.sock
python3 test/functional/qml_test_onboarding.py --socket-path /tmp/test_bridge.sock
```

When `--socket-path` is provided the harness connects to the existing socket
and does **not** launch or terminate the application.

### Available tests

| Script | Description |
|---|---|
| `qml_test_bridge_sanity.py` | Bridge protocol smoke test: list_objects, get_current_page, get_property, error handling, wait_for_page timeout |
| `qml_test_onboarding.py` | Walks through the full onboarding flow (Cover → Strengthen → Blockclock → StorageLocation → StorageAmount → Connection) |

## Prerequisite: `objectName` annotations

The test bridge locates QML elements by their `objectName` property. Every
interactive element that tests need to access **must** have an `objectName`
set:

```qml
ContinueButton {
    objectName: "importWalletButton"
    text: qsTr("Import wallet")
    onClicked: root.push(importWallet)
}

TextField {
    objectName: "walletNameField"
}

CoreText {
    objectName: "importErrorLabel"
}
```

When adding new QML pages or controls, include `objectName` for any element
that a test might need to interact with or inspect.

### `InformationPage` button naming

`InformationPage` exposes a `buttonObjectName` property (default:
`"continueButton"`) that controls the `objectName` of its built-in
`ContinueButton`. Each page should override it with a unique name so tests
can click the correct button unambiguously:

```qml
InformationPage {
    objectName: "onboardingStrengthen"
    buttonObjectName: "onboardingStrengthenButton"
    ...
}
```

### Onboarding pages

The following `objectName` values are set on the onboarding flow pages and
their buttons:

| Page | `objectName` | Button `objectName` |
|---|---|---|
| OnboardingCover | `onboardingCover` | `onboardingCoverButton` |
| OnboardingStrengthen | `onboardingStrengthen` | `onboardingStrengthenButton` |
| OnboardingBlockclock | `onboardingBlockclock` | `onboardingBlockclockButton` |
| OnboardingStorageLocation | `onboardingStorageLocation` | `onboardingStorageLocationButton` |
| OnboardingStorageAmount | `onboardingStorageAmount` | `onboardingStorageAmountButton` |
| OnboardingConnection | `onboardingConnection` | `onboardingConnectionButton` |

## Source files

| File | Description |
|---|---|
| `qml/test/testbridge.h` | `TestBridge` class declaration |
| `qml/test/testbridge.cpp` | `TestBridge` implementation |
| `qml/bitcoin.cpp` | Integration point (`-test-automation` arg, bridge init) |
| `CMakeLists.txt` | `ENABLE_TEST_AUTOMATION` option and conditional compilation |
| `test/functional/qml_driver.py` | Python `QmlDriver` client |
| `test/functional/qml_test_harness.py` | Shared test harness (launch / attach, cleanup, tree dump) |
| `test/functional/qml_test_bridge_sanity.py` | Bridge protocol sanity test |
| `test/functional/qml_test_onboarding.py` | Onboarding flow walk-through test |

## Security considerations

- The test bridge is **never compiled** in default builds (`ENABLE_TEST_AUTOMATION` defaults to `OFF`).
- Even when compiled in, it is **never activated** unless `-test-automation` is explicitly passed.
- The Unix domain socket is local-only and subject to filesystem permissions.
- Release builds and CI artifact builds should **not** enable `ENABLE_TEST_AUTOMATION`.
