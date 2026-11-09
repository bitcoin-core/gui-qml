# CI checks

The workflow in `.github/workflows/ci.yml` runs separate app lint, Core lint,
clang-tidy and sanitizer jobs alongside the Qt 6.2, 6.4 and 6.10 build/test
matrix. The Qt 6.10 build also runs [QML static analysis](qml-lint.md).

## Repository and Core lint

Run both lint scopes with the pinned Core CI dependencies:

```sh
./ci/lint.py
```

This builds the image from `bitcoin/ci/lint_imagefile`, then runs:

- Core's complete, unchanged lint runner against the pinned `bitcoin/` checkout.
- Core's applicable repository checks against the app: configuration includes,
  Ruff, controlled test cleanup, filesystem APIs, assertions, commit messages,
  scripted diffs, whitespace, final newlines, Markdown links and release notes.
- Core's filename/permission and Python type checks against the app, plus shell
  locale and ShellCheck checks when shell scripts are present.

Use `--scope app` or `--scope core` to select one scope. Set `COMMIT_RANGE` to
the app commits being reviewed, for example `origin/qt6..HEAD`. The GitHub job
derives this range from the event's base commit. Core has its own Git history;
the app commit range is never passed into the submodule.

The app adapter copies the pinned Rust lint runner into `build/lint/` and
changes repository paths only. App Python settings extend `bitcoin/ruff.toml`.
The controlled-cleanup exception points to `qml_test_harness.py`, which only
removes directories registered by its creation helper and rejects replaced
symlinks. Positive and negative probes verify that the adapted checks still
catch real violations. Core-specific argument documentation, subtree, RPC and
source-layout policies run in the Core scope rather than against empty app
paths. Core's files remain unchanged.

The app lint job also validates all GitHub workflows with actionlint 1.7.12.

## Clang-tidy

Install the dependencies from Core's `00_setup_env_native_tidy.sh` and the
app's Qt dependencies, then run:

```sh
python3 ci/configure.py --mode tidy --build-dir build-tidy
cmake --build build-tidy --parallel 2
python3 ci/clang_tidy.py --build-dir build-tidy --jobs 2
```

The configuration reads the compiler version, options and developer feature
settings from the pinned Core CI files. The checker builds and tests the
matching Bitcoin tidy plugin, then checks all configured app compilation units
and app headers using `bitcoin/src/.clang-tidy`. Generated code and Core's own
compilation units are excluded from this app pass. An empty selection fails.
Per-file diagnostics and results are retained in `build-tidy/clang-tidy/`.

## Sanitizers

```sh
python3 ci/configure.py --mode asan --build-dir build-asan
cmake --build build-asan --parallel 2
```

This uses Core's Clang version and address, leak, undefined-behavior, float
division and integer checks. The app CI configuration uses `-O1 -g1` to limit
compiler memory use on hosted runners. IPC, app tests and test automation are
enabled. The workflow runs CTest and the real `qml_test_shutdown.py` lifecycle
test with Core's 512 KiB stack limit, the original timeout, and stderr/exit-status
assertions.

Use Core's runtime options and suppression files, as shown in the workflow.
[Sanitizer exceptions](sanitizers.md) documents the narrowly scoped Qt hash
exception. The workflow uploads CTest and clang-tidy diagnostics even on failure.
