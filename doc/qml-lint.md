# QML static analysis

The application supports Qt 6.2. The optional lint target uses Qt 6.10 or
newer for its current type analysis and Qt Quick lint plugin:

```sh
cmake -S . -B build -DENABLE_QML_LINT=ON
cmake --build build --target qml_lint
```

Install the Qt declarative development tools and qmllint plugins as well as
the application's normal build dependencies. CI runs this target in its
Qt 6.10 job and retains the JSON reports in `build/qml-lint/*/qmllint.json`.
The Qt 6.2 and Qt 6.4 jobs continue to build and test the application.
The other repository, Core, clang-tidy and sanitizer jobs are described in
[CI checks](ci-checks.md).

The target checks every QML and JavaScript file shipped in the resource
collections, using both the Qt 6.2 and Qt 6.5 compatibility aliases. It stages
the actual resource paths so relative imports and aliases resolve as they do
in the application. Test-only QML fixtures use separate mock registrations;
they are exercised by the QML runtime tests rather than this production lint
target.

C++ properties and methods come from generated moc metadata. The foreign
declarations in `cmake/qmltypes.h` describe the existing imperative QML
registrations; keep them in sync with `RegisterQmlTypes()`. They do not add
runtime registrations. `ApplicationModels` gives QML a typed, per-engine view
of the existing context models without taking ownership of them.

Delegates and loaded forms that need typed access use separate QML files with
explicit inputs and signals. Qt 6.2 and Qt 6.4 do not reliably support casts to
inline component types, even where newer Qt versions accept them.

Warnings and errors fail the target, including unused imports and unresolved
unqualified names. Local qmllint settings cannot relax CI's policy. Two
migration diagnostics remain informational to preserve Qt 6.2 support:

- Advice to replace `PropertyChanges { target: ... }` with target-less syntax.
  Qt 6.2 needs the target-based form. qmllint cannot fully analyze this legacy
  custom parser syntax; state behavior still needs runtime tests.
- Unqualified-access diagnostics whose sole suggested fix is
  `pragma ComponentBehavior: Bound`. This pragma was introduced in Qt 6.4.
  These are references to IDs captured by nested components, rather than
  unknown global names. All other unqualified-access diagnostics still fail.

Both kinds remain in the JSON reports with `qt62Compatibility: true` and are
counted separately in the command output. Remove these exceptions when the
minimum Qt version permits the migrations. The target also runs small real
qmllint probes to verify resource aliases and compatibility syntax pass while
unknown names, missing types/properties, unused imports and syntax errors fail.

References: [qmllint diagnostics](https://doc.qt.io/qt-6.10/qtqml-tooling-qmllint.html)
and [Qt's explanation of ComponentBehavior](https://www.qt.io/blog/compiling-qml-to-c-fixing-unqualfied-access).
