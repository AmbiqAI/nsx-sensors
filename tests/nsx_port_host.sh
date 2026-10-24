#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
for prefix in 3 5; do
    cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
        -DAM_HAL_IOM_MAX_OFFSETSIZE="$prefix" -I "$root/include" -I "$root/tests/nsx_port/stubs" \
        "$root/src/ports/nsx.c" "$root/src/common/bus.c" "$root/src/ledstick/ledstick.c" \
        "$root/src/ina228/ina228.c" "$root/src/bme688/bme688.c" "$root/src/bme688/compensation.c" \
        "$root/tests/nsx_port/test_port.c" -lm -o "$out/port-$prefix"
    "$out/port-$prefix"
done
# Ordinary firmware builds do not emit an SDK package export.
cmake -S "$root/tests/nsx_port" -B "$out/firmware"
cmake --build "$out/firmware" --parallel
"$out/firmware/port_contract"
cmake -S "$root/tests/nsx_port" -B "$out/build" -DNSX_PORT_PACKAGE_TEST=ON \
    -DCMAKE_INSTALL_PREFIX="$out/install" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$out/build" --parallel
"$out/build/port_contract"
cmake --install "$out/build"
mv "$out/install" "$out/relocated"
cmake -S "$root/tests/nsx_port" -B "$out/consumer" \
    -DNSX_PORT_INSTALLED="$out/relocated" -DCMAKE_PREFIX_PATH="$out/relocated"
cmake --build "$out/consumer" --parallel
"$out/consumer/port_contract"
