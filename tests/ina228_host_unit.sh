#!/usr/bin/env bash
# Host unit tests for the INA228 driver register math.
#
# Compiles src/nsx_ina228.c against the stub transport in
# tests/ina228_host_unit/ and runs the resulting binary on the host, so the
# compiler must be a native one (default cc), never a bare-metal cross. This
# checks the driver's arithmetic against the datasheet; it proves nothing
# about I2C transport or sensor behavior on real hardware.
#
# Usage: tests/ina228_host_unit.sh [compiler]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$repo_root/tests/ina228_host_unit"
out_dir="$repo_root/build/ina228-host-unit"

compiler="${1:-${CC:-cc}}"
if ! command -v "$compiler" >/dev/null 2>&1; then
    echo "ina228 host unit: compiler '$compiler' not found" >&2
    exit 1
fi

rm -rf "$out_dir"
mkdir -p "$out_dir"

"$compiler" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -I "$test_dir" \
    -I "$repo_root/includes-api" \
    "$repo_root/src/nsx_ina228.c" \
    "$test_dir/test_nsx_ina228.c" \
    -lm \
    -o "$out_dir/test_nsx_ina228"

"$out_dir/test_nsx_ina228"
