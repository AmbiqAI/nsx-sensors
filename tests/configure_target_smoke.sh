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
#   tests/configure_target_smoke.sh <consumer_root> <toolchain> <board> <firmware-relative-path> <driver> [driver ...]
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
firmware_relative=${4:?provide the unstripped executable path relative to the build directory}
shift 4
[ "$#" -gt 0 ] || { echo 'target smoke: select at least one driver (e.g. bme688 port_nsx_i2c)' >&2; exit 1; }

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
require_tool python3
if ! command -v arm-none-eabi-nm >/dev/null 2>&1 && ! command -v llvm-nm >/dev/null 2>&1; then
    fail "arm-none-eabi-nm or llvm-nm is required to inspect firmware"
fi

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

# Never accept an old executable outside the freshly rebuilt output tree.
firmware=$(python3 - "$build_dir" "$firmware_relative" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve()
relative = Path(sys.argv[2])
path = (root / relative).resolve()
if relative.is_absolute() or not path.is_relative_to(root):
    sys.exit("target smoke: firmware path must stay inside the build directory")
print(path)
PY
)

# An archive's presence does not prove that its code survived the firmware link.
# Check the explicitly selected executable and driver implementations instead.
python3 "$repo_root/tests/verify_firmware_link.py" "$firmware" "$@"

echo "target smoke passed: $toolchain / $board"
