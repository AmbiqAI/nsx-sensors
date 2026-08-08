#!/usr/bin/env bash
# Module compile smoke against a real NSX SDK checkout.
#
# Compiles every nsx-sensors translation unit, including the vendored TDK
# subset, against genuine nsx-core / nsx-i2c / nsx-spi and AmbiqSuite headers
# for a chosen SoC and toolchain. This is the middle rung between the
# dependency-free vendor smoke and the full consumer build:
#
#   vendor_compile_smoke.sh   vendored subset only, no NSX headers
#   module_compile_smoke.sh   all module sources, real NSX + AmbiqSuite headers
#   configure_target_smoke.sh full NSX consumer configure, build, and link
#
# It compiles only; it does not link and it does not flash. A clean run means
# the sources are toolchain- and header-compatible. It is not evidence that any
# driver works against real silicon.
#
# Usage:
#   tests/module_compile_smoke.sh <nsx_ambiq_sdk_root> <toolchain> [soc]
#
# toolchain is one of: arm-none-eabi-gcc | armclang | atfe
# soc defaults to apollo510.
#
# For ATfE, set ATFE_ROOT or ATFE_CLANG. An ATfE request is never downgraded to
# a host clang and never silently skipped.
set -euo pipefail

sdk_root=${1:?usage: module_compile_smoke.sh <nsx_ambiq_sdk_root> <toolchain> [soc]}
toolchain=${2:?usage: module_compile_smoke.sh <nsx_ambiq_sdk_root> <toolchain> [soc]}
soc=${3:-apollo510}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="$repo_root/build/module-compile-${toolchain}-${soc}"

fail() {
    echo "module compile smoke: $*" >&2
    exit 1
}

test -d "$sdk_root" || fail "SDK root '$sdk_root' is not a directory"

ambiqsuite="$sdk_root/modules/nsx-ambiqsuite/sdk"
test -d "$ambiqsuite" || fail "AmbiqSuite payload not found at '$ambiqsuite'"

soc_facts="$sdk_root/cmake/socs/facts/${soc}.cmake"
test -f "$soc_facts" || fail "no SoC facts for '$soc' at '$soc_facts'"

soc_descriptor="$sdk_root/cmake/socs/${soc}.cmake"
test -f "$soc_descriptor" || fail "no SoC descriptor for '$soc' at '$soc_descriptor'"

# Some SoCs deliberately reuse another part's MCU payload (apollo510b builds on
# the apollo510 HAL), so resolve the directory through the SDK descriptor's
# NSX_AMBIQ_PART_NAME / NSX_AMBIQ_MCU_DIR pair rather than assuming mcu/<soc>.
part_name="$(sed -n 's|^set(NSX_AMBIQ_PART_NAME "\([^"]*\)")$|\1|p' \
    "$soc_descriptor" | head -1)"
[ -n "$part_name" ] || fail "could not read NSX_AMBIQ_PART_NAME from '$soc_descriptor'"
mcu_name="$(sed -n 's|^set(NSX_AMBIQ_MCU_DIR "${NSX_AMBIQSUITE_ROOT}/mcu/\([^"]*\)")$|\1|p' \
    "$soc_descriptor" | head -1)"
[ -n "$mcu_name" ] || fail "could not read NSX_AMBIQ_MCU_DIR from '$soc_descriptor'"
mcu_name="${mcu_name/\$\{NSX_AMBIQ_PART_NAME\}/$part_name}"
mcu_dir="$ambiqsuite/mcu/$mcu_name"
test -d "$mcu_dir" || fail \
    "SoC '$soc' maps to mcu/$mcu_name, which is not staged in '$ambiqsuite/mcu'"

# Reuse the SDK's own single source of truth for the SoC compile definitions and
# core selection instead of hard-coding a second copy that can drift. Uses a
# read loop rather than mapfile so the script runs on bash 3.2 (macOS).
soc_defines=()
while IFS= read -r define; do
    [ -n "$define" ] && soc_defines+=("$define")
done < <(
    sed -n '/^set(NSX_SOC_COMPILE_DEFINITIONS$/,/^)$/p' "$soc_facts" \
        | sed '1d;$d' \
        | tr -d '[:blank:]'
)
[ "${#soc_defines[@]}" -gt 0 ] || fail "no NSX_SOC_COMPILE_DEFINITIONS in '$soc_facts'"

soc_core="$(sed -n 's/^set(NSX_SOC_CORE "\(.*\)")$/\1/p' "$soc_facts" | head -1)"
[ -n "$soc_core" ] || fail "no NSX_SOC_CORE in '$soc_facts'"

defines=()
for define in "${soc_defines[@]}"; do
    defines+=("-D${define}")
done

includes=(
    -I "$repo_root/includes-api"
    -I "$repo_root/vendor/tdk-icm45605"
    -isystem "$sdk_root/modules/nsx-core/includes-api"
    -isystem "$sdk_root/modules/nsx-i2c/includes-api"
    -isystem "$sdk_root/modules/nsx-spi/includes-api"
    -isystem "$sdk_root/modules/nsx-soc-hal/includes-api"
    -isystem "$sdk_root/modules/nsx-cmsis-core/Include"
    -isystem "$mcu_dir"
    -isystem "$mcu_dir/hal"
    -isystem "$mcu_dir/regs"
    -isystem "$ambiqsuite/utils"
    -isystem "$ambiqsuite/CMSIS/AmbiqMicro/Include"
)

case "$toolchain" in
    arm-none-eabi-gcc)
        command -v arm-none-eabi-gcc >/dev/null 2>&1 \
            || fail "arm-none-eabi-gcc not found on PATH"
        compiler="arm-none-eabi-gcc"
        arch_flags=(-mcpu="$soc_core" -mthumb -mfloat-abi=hard)
        ;;
    armclang)
        command -v armclang >/dev/null 2>&1 || fail "armclang not found on PATH"
        compiler="armclang"
        arch_flags=(--target=arm-arm-none-eabi -mcpu="$soc_core")
        ;;
    atfe)
        compiler="${ATFE_CLANG:-}"
        if [ -z "$compiler" ] && [ -n "${ATFE_ROOT:-}" ]; then
            compiler="$ATFE_ROOT/bin/clang"
        fi
        [ -n "$compiler" ] || fail \
            "ATfE requested but neither ATFE_CLANG nor ATFE_ROOT is set"
        [ -x "$compiler" ] || fail "ATfE clang '$compiler' is not executable"
        # Match on the version banner only, never on the install path, so a
        # directory name cannot make a host clang look like ATfE.
        banner="$("$compiler" --version 2>/dev/null | grep -v '^InstalledDir:' || true)"
        printf '%s\n' "$banner" \
            | grep -qiE 'Arm Toolchain ID:|Arm Toolchain for Embedded|LLVM Embedded Toolchain for Arm' \
            || fail "'$compiler' is not an Arm Toolchain for Embedded clang"
        # ATfE selects a multilib from the full triple plus FPU spelling.
        case "$soc_core" in
            cortex-m55|cortex-m85)
                arch_flags=(
                    --target=thumbv8.1m.main-none-eabi
                    -mcpu="$soc_core"
                    -mfloat-abi=hard
                    -mfpu=fp-armv8-fullfp16-d16
                )
                ;;
            cortex-m4|cortex-m4f)
                arch_flags=(
                    --target=thumbv7em-none-eabi
                    -mcpu="$soc_core"
                    -mfloat-abi=hard
                    -mfpu=fpv4-sp-d16
                )
                ;;
            *)
                fail "no ATfE multilib mapping for core '$soc_core'"
                ;;
        esac
        ;;
    *)
        fail "unsupported toolchain '$toolchain' (expected arm-none-eabi-gcc, armclang, or atfe)"
        ;;
esac

rm -rf "$out_dir"
mkdir -p "$out_dir"

sources=("$repo_root"/src/*.c "$repo_root"/vendor/tdk-icm45605/*.c)
[ "${#sources[@]}" -eq 7 ] || fail "expected 7 translation units, found ${#sources[@]}"

for source in "${sources[@]}"; do
    echo "compiling $(basename "$source") [$toolchain/$soc/$soc_core]"
    "$compiler" -c -std=c11 \
        -Wall -Wextra -Werror \
        "${arch_flags[@]}" \
        "${defines[@]}" \
        "${includes[@]}" \
        "$source" \
        -o "$out_dir/$(basename "${source%.c}").o"
done

produced="$(find "$out_dir" -name '*.o' | wc -l | tr -d '[:blank:]')"
[ "$produced" -eq "${#sources[@]}" ] \
    || fail "expected ${#sources[@]} objects, produced $produced"

echo "module compile smoke passed: $toolchain / $soc (${#sources[@]} objects)"
