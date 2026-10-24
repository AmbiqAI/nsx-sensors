#!/usr/bin/env bash
# Offline native portable core tests with synthetic registers.
# Usage: tests/bme688_host_unit.sh [native compiler] [--sanitize]
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${1:-${CC:-cc}}"
flags=()
suffix=""
if [ "${2:-}" = --sanitize ]; then
    flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    suffix="-sanitize"
elif [ -n "${2:-}" ]; then
    echo "unsupported option: $2" >&2
    exit 1
fi
out_dir="$repo_root/build/bme688-host-unit${suffix}"
mkdir -p "$out_dir"
"$compiler" -std=c11 -Wall -Wextra -Werror -pedantic "${flags[@]}" \
    -I "$repo_root/tests/bme688_host_unit" -I "$repo_root/include" \
    "$repo_root/src/bme688/bme688.c" "$repo_root/src/bme688/compensation.c" \
    "$repo_root/src/common/bus.c" "$repo_root/tests/bme688_host_unit/test_nsx_bme688.c" \
    -lm -o "$out_dir/test_nsx_bme688"
"$out_dir/test_nsx_bme688"
