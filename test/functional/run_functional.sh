#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
shift 1 2>/dev/null || true
TESTS=("$@")

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

# Keep Unix socket paths short on macOS.
TMPDIR="/tmp/qmltest"
export TMPDIR
mkdir -p "$TMPDIR"

BITCOIND="${BITCOIND:-$ROOT_DIR/$BUILD_DIR/bin/bitcoind}"
BITCOIN_CORE_APP="${BITCOIN_CORE_APP:-$ROOT_DIR/$BUILD_DIR/bin/bitcoin-core-app}"

if [[ ! -x "$BITCOIND" ]]; then
  echo "bitcoind not found at $BITCOIND" >&2
  exit 1
fi
if [[ ! -x "$BITCOIN_CORE_APP" ]]; then
  echo "bitcoin-core-app not found at $BITCOIN_CORE_APP" >&2
  exit 1
fi

export BITCOIND
export BITCOIN_CORE_APP
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

if [[ ${#TESTS[@]} -eq 0 ]]; then
  while IFS= read -r line; do
    TESTS+=("$(basename "$line")")
  done < <(find test/functional -maxdepth 1 -name 'qml_test_*.py' | sort)
fi

FAILED=0
for t in "${TESTS[@]}"; do
  script="test/functional/$t"
  [[ -f "$script" ]] || { echo "Missing $script" >&2; FAILED=1; continue; }
  echo "===== $t ====="
  if ! python3 "$script"; then
    echo "FAILED: $t" >&2
    FAILED=1
  fi
done

if [[ $FAILED -ne 0 ]]; then
  echo "One or more functional tests failed." >&2
  exit 1
fi
echo "All functional tests passed."
