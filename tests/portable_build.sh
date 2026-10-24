#!/usr/bin/env bash
# Offline portable API, selected-driver, add_subdirectory and relocated install tests.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cmake -S "$root" -B "$out/build" -DNSX_SENSORS_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$out/install" -DCMAKE_INSTALL_LIBDIR=lib
# Standalone configuration must not enable the NSX aggregate implicitly.
grep -q '^NSX_SENSORS_LEGACY_NSX:BOOL=OFF$' "$out/build/CMakeCache.txt"
# The normal NSX entry point must provide its advertised target without overrides.
cmake -S "$root/tests/nested_contract" -B "$out/nsx-default"
grep -q '^NSX_SENSORS_LEGACY_NSX:BOOL=ON$' "$out/nsx-default/CMakeCache.txt"
cmake --build "$out/build" --parallel
ctest --test-dir "$out/build" --output-on-failure
cmake --install "$out/build"
mv "$out/install" "$out/relocated"
cmake -S "$root/examples/baremetal" -B "$out/installed" -DCMAKE_PREFIX_PATH="$out/relocated"
cmake --build "$out/installed" --parallel
"$out/installed/sensor_example"
"$out/installed/sensor_cpp"
cmake -S "$root/examples/baremetal" -B "$out/nested" -DNSX_SENSORS_SOURCE="$root"
cmake --build "$out/nested" --parallel
"$out/nested/sensor_example"
"$out/nested/sensor_cpp"
for driver in BME688 LEDSTICK INA228 MPU6050 MAX86150 NONE; do
    bme=OFF; led=OFF; ina=OFF; mpu=OFF; max=OFF
    if [[ "$driver" == BME688 ]]; then bme=ON; fi
    if [[ "$driver" == LEDSTICK ]]; then led=ON; fi
    if [[ "$driver" == INA228 ]]; then ina=ON; fi
    if [[ "$driver" == MPU6050 ]]; then mpu=ON; fi
    if [[ "$driver" == MAX86150 ]]; then max=ON; fi
    cmake -S "$root" -B "$out/$driver" -DNSX_SENSOR_BME688="$bme" -DNSX_SENSOR_LEDSTICK="$led" -DNSX_SENSOR_INA228="$ina" -DNSX_SENSOR_MPU6050="$mpu" -DNSX_SENSOR_MAX86150="$max" \
        -DNSX_SENSORS_TESTS=ON
    cmake --build "$out/$driver" --parallel
    if [[ "$driver" != NONE ]]; then ctest --test-dir "$out/$driver" --output-on-failure; fi
    if [[ "$driver" == LEDSTICK ]]; then
        cmake --install "$out/$driver" --prefix "$out/led-only"
        test ! -f "$out/led-only/include/nsx/sensors/bme688.h"
        if cmake -S "$root/examples/baremetal" -B "$out/missing-component" \
            -DCMAKE_PREFIX_PATH="$out/led-only" > "$out/missing-component.log" 2>&1; then
            echo "package unexpectedly accepted missing BME688 component" >&2
            exit 1
        fi
        grep -q 'nsx_sensors_FOUND to FALSE' "$out/missing-component.log"
    fi
done
# Sanitizers cover the shared bus and all actual portable driver libraries.
cmake -S "$root" -B "$out/sanitize" -DNSX_SENSORS_TESTS=ON \
    -DCMAKE_C_FLAGS='-Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
    -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
cmake --build "$out/sanitize" --parallel
ctest --test-dir "$out/sanitize" --output-on-failure
