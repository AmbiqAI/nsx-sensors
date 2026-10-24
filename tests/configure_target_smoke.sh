#!/usr/bin/env bash
# NSX consumer configure and build smoke for nsx-sensors.
#
# Configures and builds a real NSX consumer application against this working
# tree for a requested toolchain and board, so the module is proven to link into
# a genuine NSX dependency graph rather than a stub.
#
# This is opt-in: it needs an NSX consumer workspace and a cross toolchain that
# a default CI runner does not have. It never passes silently. A missing
# workspace, a missing cmake, or a missing toolchain is a hard failure, so
# "green" always means the build really ran.
#
# Usage:
#   tests/configure_target_smoke.sh <consumer_root> <toolchain> <board>
#
# toolchain is one of: arm-none-eabi-gcc | armclang | atfe
#
# ATfE (Arm Toolchain for Embedded) ships its own clang. Point ATFE_ROOT or
# ATFE_CLANG at it, or put an ATfE clang on PATH. An ATfE request is never
# quietly downgraded to the host clang or skipped.
set -euo pipefail

consumer_root=${1:?usage: configure_target_smoke.sh <consumer_root> <toolchain> <board>}
toolchain=${2:?usage: configure_target_smoke.sh <consumer_root> <toolchain> <board>}
board=${3:?usage: configure_target_smoke.sh <consumer_root> <toolchain> <board>}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/target-${toolchain}-${board}"

fail() {
    echo "target smoke: $*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "required tool '$1' not found on PATH"
}

test -d "$consumer_root" || fail "consumer root '$consumer_root' is not a directory"
require_tool cmake

case "$toolchain" in
    arm-none-eabi-gcc)
        require_tool arm-none-eabi-gcc
        ;;
    armclang)
        require_tool armclang
        ;;
    atfe)
        atfe_clang="${ATFE_CLANG:-}"
        if [ -z "$atfe_clang" ] && [ -n "${ATFE_ROOT:-}" ]; then
            atfe_clang="$ATFE_ROOT/bin/clang"
        fi
        if [ -z "$atfe_clang" ]; then
            atfe_clang="$(command -v clang || true)"
        fi
        [ -n "$atfe_clang" ] || fail \
            "ATfE requested but no clang found; set ATFE_ROOT or ATFE_CLANG"
        [ -x "$atfe_clang" ] || fail "ATfE clang '$atfe_clang' is not executable"
        # A host clang is not ATfE. Refuse to let a host toolchain masquerade as
        # a validated ATfE build. Match on the version banner only, never on the
        # install path, so a directory name cannot fake the identification.
        atfe_banner="$("$atfe_clang" --version 2>/dev/null | grep -v '^InstalledDir:' || true)"
        [ -n "$atfe_banner" ] || fail "'$atfe_clang' did not report a version"
        printf '%s\n' "$atfe_banner" \
            | grep -qiE 'Arm Toolchain ID:|Arm Toolchain for Embedded|LLVM Embedded Toolchain for Arm' \
            || fail "'$atfe_clang' is not an Arm Toolchain for Embedded clang"
        "$atfe_clang" --print-targets 2>/dev/null | grep -qi 'arm' \
            || fail "'$atfe_clang' does not target Arm; this is not an ATfE toolchain"
        export ATFE_CLANG="$atfe_clang"
        ;;
    *)
        fail "unsupported toolchain '$toolchain' (expected arm-none-eabi-gcc, armclang, or atfe)"
        ;;
esac

rm -rf "$build_dir"

cmake -S "$consumer_root" -B "$build_dir" \
    -DNSX_SENSORS_ROOT="$repo_root" \
    -DNSX_TOOLCHAIN="$toolchain" \
    -DNSX_BOARD="$board"

cmake --build "$build_dir" --parallel 2

# The module must actually be part of the consumer build, not silently dropped.
find "$build_dir" -name 'libnsx_sensors.a' -print -quit | grep -q . \
    || fail "libnsx_sensors.a was not produced for $toolchain/$board"

echo "target smoke passed: $toolchain / $board"
