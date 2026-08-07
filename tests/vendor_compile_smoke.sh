#!/usr/bin/env bash
# Standalone compile smoke for the vendored TDK ICM-45605 eMD subset.
#
# The vendored subset must be self-contained: it must not need NSX, AmbiqSuite,
# CMSIS, or CMSIS-DSP headers, and it must compile warning-clean as C11. This
# proves the vendoring boundary is honest. It proves nothing about sensor
# behavior on real hardware.
#
# Usage: tests/vendor_compile_smoke.sh [compiler]
# The compiler defaults to $CC, then arm-none-eabi-gcc, then cc.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vendor_dir="$repo_root/vendor/tdk-icm45605"
out_dir="$repo_root/build/vendor-compile-smoke"

compiler="${1:-${CC:-}}"
if [ -z "$compiler" ]; then
    if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        compiler="arm-none-eabi-gcc"
    else
        compiler="cc"
    fi
fi

if ! command -v "$compiler" >/dev/null 2>&1; then
    echo "vendor compile smoke: compiler '$compiler' not found" >&2
    exit 1
fi

# Bare-metal cross compilers need a target CPU to emit an object file.
target_flags=()
case "$(basename "$compiler")" in
    arm-none-eabi-*) target_flags=(-mcpu=cortex-m4 -mthumb --specs=nosys.specs -c) ;;
    *) target_flags=(-c) ;;
esac

rm -rf "$out_dir"
mkdir -p "$out_dir"

sources=(
    "$vendor_dir/inv_imu_driver.c"
    "$vendor_dir/inv_imu_transport.c"
)

for source in "${sources[@]}"; do
    test -f "$source" || { echo "missing vendor source: $source" >&2; exit 1; }
    echo "compiling $(basename "$source") with $compiler"
    "$compiler" \
        "${target_flags[@]}" \
        -std=c11 \
        -Wall \
        -Wextra \
        -Wno-unused-parameter \
        -I "$vendor_dir" \
        "$source" \
        -o "$out_dir/$(basename "${source%.c}").o"
done

# Every vendor header that is intended for direct inclusion must be
# self-contained. The two register maps are deliberately excluded: upstream
# includes them from inv_imu_defs.h after <stdint.h>, and they are never
# included directly. That is a vendor property, not something to patch here.
for header in "$vendor_dir"/*.h; do
    name="$(basename "$header")"
    case "$name" in
        inv_imu_regmap_le.h|inv_imu_regmap_be.h) continue ;;
    esac
    echo "checking self-contained header $name"
    printf '#include "%s"\nint main(void) { return 0; }\n' "$name" \
        > "$out_dir/header_check.c"
    "$compiler" \
        "${target_flags[@]}" \
        -std=c11 \
        -Wall \
        -I "$vendor_dir" \
        "$out_dir/header_check.c" \
        -o "$out_dir/header_check.o"
done

# The excluded register maps must stay reachable only through inv_imu_defs.h,
# so the exclusion above cannot hide a broken include graph.
grep -q '#include "inv_imu_regmap_le.h"' "$vendor_dir/inv_imu_defs.h"
stray_includes="$(grep -rl '^[[:space:]]*#include[[:space:]]*"inv_imu_regmap_' \
    "$vendor_dir" | grep -v '/inv_imu_defs\.h$' || true)"
if [ -n "$stray_includes" ]; then
    echo "register maps are included outside inv_imu_defs.h:" >&2
    echo "$stray_includes" >&2
    exit 1
fi

echo "vendor compile smoke passed with $compiler"
