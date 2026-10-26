#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cmake -S "$root/tests/firmware_link" -B "$out/build" \
    -DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_CXX_COMPILER=arm-none-eabi-g++ \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
cmake --build "$out/build" --parallel
python3 "$root/tests/verify_firmware_link.py" "$out/build/firmware" --map "$out/build/firmware.map" \
    bme688 ledstick ina228 mpu6050 max86150 icm45605 tsl254x t5838
reject() {
    local artifact=$1 reason=$2 map=${3:-"$out/build/firmware.map"}
    if python3 "$root/tests/verify_firmware_link.py" "$artifact" --map "$map" bme688 > "$out/rejected.log" 2>&1; then
        echo "firmware check accepted $artifact" >&2
        exit 1
    fi
    grep -q "$reason" "$out/rejected.log"
}
reject "$out/build/unused" 'missing linked functions'
reject "$out/build/replacement" 'wrong defining object' "$out/build/replacement.map"
reject "$out/build/firmware" 'does not match ELF address' "$out/build/replacement.map"
reject "$out/build/firmware" 'No such file' "$out/no-map"
reject "$out/build/data_symbol" 'missing linked functions'
reject "$out/build/sensors/libnsx_sensor_bme688.a" 'expected an ELF32'
reject "$out/build/CMakeFiles/unused.dir/unused.c.o" 'expected a linked Arm executable'
reject "$out/absent.elf" 'No such file'
cc "$root/tests/firmware_link/unused.c" -o "$out/host"
reject "$out/host" 'expected'
cp "$out/build/firmware" "$out/stripped"
arm-none-eabi-strip --strip-all "$out/stripped"
reject "$out/stripped" 'missing linked functions'
echo 'firmware link contracts passed (synthetic Arm link, not a board firmware)'
