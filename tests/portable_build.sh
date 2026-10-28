#!/usr/bin/env bash
# Offline portable API, selected-driver, add_subdirectory and relocated install tests.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cmake -S "$root" -B "$out/build" -DNSX_SENSORS_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$out/install" -DCMAKE_INSTALL_LIBDIR=lib
# Standalone configuration must not enable the SDK-dependent adapter.
grep -q '^NSX_SENSOR_PORT_NSX:BOOL=OFF$' "$out/build/CMakeCache.txt"
# The normal NSX entry point must provide its advertised target without overrides.
cmake -S "$root/tests/nested_contract" -B "$out/nsx-default"
grep -q '^NSX_SENSOR_PORT_NSX:BOOL=ON$' "$out/nsx-default/CMakeCache.txt"
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
# Foreign TDK-like headers and symbols must coexist in either include order,
# both from the source tree and a relocated installation (C and C++).
for mode in source installed; do
    if [[ "$mode" == source ]]; then
        isolation_arg="-DNSX_SENSORS_SOURCE=$root"
    else
        isolation_arg="-DCMAKE_PREFIX_PATH=$out/relocated"
    fi
    cmake -S "$root/tests/vendor_isolation" -B "$out/isolation-$mode" "$isolation_arg"
    cmake --build "$out/isolation-$mode" --parallel
    ctest --test-dir "$out/isolation-$mode" --output-on-failure
done
if nm -g --defined-only "$out/build/libnsx_sensor_icm45605.a" | grep -Eq ' [A-Z] inv_imu_'; then
    echo "generic TDK symbols escaped from ICM-45605" >&2
    exit 1
fi
for driver in BME688 LEDSTICK INA228 MPU6050 MAX86150 ICM42670 ICM45605 TSL254X T5838 NONE; do
    bme=OFF; led=OFF; ina=OFF; mpu=OFF; max=OFF; icm_new=OFF; icm=OFF; tsl=OFF; mic=OFF
    if [[ "$driver" == BME688 ]]; then bme=ON; fi
    if [[ "$driver" == LEDSTICK ]]; then led=ON; fi
    if [[ "$driver" == INA228 ]]; then ina=ON; fi
    if [[ "$driver" == MPU6050 ]]; then mpu=ON; fi
    if [[ "$driver" == MAX86150 ]]; then max=ON; fi
    if [[ "$driver" == ICM42670 ]]; then icm_new=ON; fi
    if [[ "$driver" == ICM45605 ]]; then icm=ON; fi
    if [[ "$driver" == TSL254X ]]; then tsl=ON; fi
    if [[ "$driver" == T5838 ]]; then mic=ON; fi
    cmake -S "$root" -B "$out/$driver" -DNSX_SENSOR_BME688="$bme" -DNSX_SENSOR_LEDSTICK="$led" -DNSX_SENSOR_INA228="$ina" -DNSX_SENSOR_MPU6050="$mpu" -DNSX_SENSOR_MAX86150="$max" -DNSX_SENSOR_ICM42670="$icm_new" -DNSX_SENSOR_ICM45605="$icm" -DNSX_SENSOR_TSL254X="$tsl" -DNSX_SENSOR_T5838="$mic" \
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
# Embedded consumers may use short enum storage. Compile the driver and its
# tests consistently; invalid-value tests must account for caller-side narrowing.
cc -std=c11 -Wall -Wextra -Werror -fshort-enums -I "$root/include" \
    "$root/src/common/bus.c" "$root/src/max86150/max86150.c" \
    "$root/tests/max86150_host/test_max86150.c" -o "$out/max86150-short-enums"
"$out/max86150-short-enums"
for driver in tsl254x t5838 icm42670; do
    extra_sources=()
    if [[ "$driver" == t5838 ]]; then extra_sources+=("$root/examples/spark/t5838_pair.c"); fi
    cc -std=c11 -Wall -Wextra -Werror -pedantic -fshort-enums -I "$root/include" \
        "$root/src/common/bus.c" "$root/src/$driver/$driver.c" \
        "$root/tests/${driver}_host/test_$driver.c" "${extra_sources[@]}" \
        -o "$out/$driver-short-enums"
    "$out/$driver-short-enums"
done
