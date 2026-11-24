# Sanitizer exceptions

Clang builds with `integer` in `SANITIZERS` use
`test/sanitizer_suppressions/ubsan.ignorelist`. The exception covers only
unsigned overflow in Qt's `QHashPrivate::hash`, whose mixing multiplications
deliberately wrap modulo the integer width. It was reproduced independently
with Qt 6.4.2 and Qt 6.10.2.

The rule uses the function's C++ symbol name and works with reduced debug
information. It does not depend on runtime symbolization. It applies to the
app and its tests. The remaining integer checks and Core's
runtime suppression files retain their usual behavior.

Core's current `SplitMix64` implementation is a free function, so its
deliberate unsigned wrap does not match Core's inherited
`InsecureRandomContext::SplitMix64` runtime rule. The app's runtime file adds
the exact free-function name. Core's inline `bitset_detail::PopCount` helper
also has no function symbol for its inherited rule, so the runtime file matches
its source header. The sanitizer controls below remain in place.

With app tests enabled, these builds add
`bitcoinqml_sanitizer_suppression_tests` to CTest. The test verifies that Qt's
hash and Core's `SplitMix64` wrap succeed while unrelated unsigned overflow,
signed overflow and implicit truncation fail. It also verifies that unsigned
overflow in a neighboring function and signed overflow in a function matching
the rule still fail.
On Linux builds with address or leak sanitization, an intentional leak in a
copy of the probe named `bitcoin-core-app` must also fail. This also catches an
accidental executable-wide app suppression.

Qt 6.4.2's software renderer retains cached textures during shutdown. This was
reproduced with a standalone Qt application, including explicit resource release
and deferred-delete processing. Qt fixed the missing cache cleanup in
[QTBUG-106968](https://github.com/qt/qtdeclarative/commit/f1b188df132c42da62197055725e5f7eebcc4249).
`test/sanitizer_suppressions/lsan` matches only allocations through
`QSGRenderContext::textureForFactory`. It does not suppress the app executable.

Append that dependency rule to Core's suppression file before running tests:

```sh
export LSAN_OPTIONS
LSAN_OPTIONS=$(python3 ci/lsan_suppressions.py --build-dir build-asan)
python3 build-asan/bitcoin/test/with_sanitizer_env.py ctest --test-dir build-asan --output-on-failure
```

The helper preserves Core's runtime options and writes the combined suppression
file inside the build directory. The sanitizer probe verifies that unrelated
application allocations still trigger LeakSanitizer with these rules active.

Qt 6.4 Quick Test retains application-global QML engine allocations after its
per-file engines are destroyed. The QML-labeled CTest entries therefore run
with LeakSanitizer scanning disabled, but keep AddressSanitizer and Undefined
Behavior Sanitizer enabled. Unit, integration, lifecycle and functional tests
continue to run with leak detection, including the intentional-leak control.

The full sanitizer configuration remains
`address,float-divide-by-zero,integer,undefined`. Continue using the pinned
Core runtime options and suppression files when running it. Sanitizer checks
only cover code paths exercised by the tests.

For the GUI's strict stderr assertions, append `:print_suppressions=0` to
`LSAN_OPTIONS`. This hides the summary table for Core's existing dependency
suppressions. Leak detection and reports for unsuppressed leaks remain active.
