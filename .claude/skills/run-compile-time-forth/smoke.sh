#!/usr/bin/env bash
# smoke.sh — build, test, and run compile-time-forth through the Makefile.
#
# The Makefile is the single interface to the build. This script never
# invokes a compiler directly; it passes TOOLCHAIN/CONFIG through to make
# and then runs the produced binaries from their computed locations.
#
# Usage:
#   .claude/skills/run-compile-time-forth/smoke.sh                # system c++, Asan
#   .claude/skills/run-compile-time-forth/smoke.sh gcc-16         # gcc-16, Asan
#   .claude/skills/run-compile-time-forth/smoke.sh gcc-16 Debug   # gcc-16, Debug
#
# Exit code 0 = build + ctest + example all passed.
set -euo pipefail

cd "$(dirname "$0")/../../.."   # repo root

TOOLCHAIN="${1:-${TOOLCHAIN:-}}"
CONFIG="${2:-${CONFIG:-Asan}}"

MAKE_ARGS=(CONFIG="$CONFIG")
if [ -n "$TOOLCHAIN" ]; then
    MAKE_ARGS+=(TOOLCHAIN="$TOOLCHAIN")
    BUILD_NAME="build-$TOOLCHAIN"
else
    BUILD_NAME="build-system"
fi
BUILD_PATH=".build/$BUILD_NAME"

echo "== make ${MAKE_ARGS[*]} (build + ctest) =="
make "${MAKE_ARGS[@]}"

echo "== run example: hello =="
HELLO="$BUILD_PATH/src/examples/$CONFIG/hello"
out="$("$HELLO")"
echo "$out"
case "$out" in
    Hello,*) ;;
    *) echo "FAIL: unexpected output from $HELLO" >&2; exit 1 ;;
esac

echo "== run test binary directly: forth_test =="
"$BUILD_PATH/src/smd/forth/$CONFIG/forth_test"

echo "SMOKE OK: $BUILD_PATH CONFIG=$CONFIG"
