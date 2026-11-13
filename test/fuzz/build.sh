#!/usr/bin/env bash
# Convenience build script for the fuzz targets on macOS (Homebrew).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

QT_PREFIX="$(brew --prefix qt)"
LLVM_PREFIX="$(brew --prefix llvm)"

SDK="$(xcrun --show-sdk-path)"
cmake -S . -B build-fuzz \
  -DCMAKE_CXX_COMPILER="${LLVM_PREFIX}/bin/clang++" \
  -DCMAKE_OSX_SYSROOT="${SDK}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_FUZZ_TESTS=ON \
  -DBUILD_APP_TESTS=OFF

cmake --build build-fuzz --parallel

echo "Built fuzz targets in build-fuzz/test/fuzz/"
echo "Run linter:"
echo "  python3 test/fuzz/qml_security_lint.py qml"
