#!/usr/bin/env python3
"""Check an unstripped Arm executable for linked sensor implementations."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys

# Actual API functions, not archive names: nsx::sensors is an interface target.
DRIVER_SYMBOLS = {
    "bme688": "bme688_validate_config",
    "ledstick": "ledstick_validate_config",
    "ina228": "ina228_validate_config",
    "mpu6050": "mpu6050_validate_config",
    "max86150": "max86150_validate_config",
    "icm45605": "icm45605_validate_config",
    "tsl254x": "tsl254x_validate_config",
    "t5838": "t5838_validate_config",
    "port_nsx_i2c": "nsx_sensor_nsx_bind_i2c",
    "port_nsx_spi": "nsx_sensor_nsx_bind_spi",
}


def verify(firmware: Path, drivers: list[str], nm: str) -> None:
    with firmware.open("rb") as stream:
        header = stream.read(20)
    if len(header) < 20 or header[:4] != b"\x7fELF" or header[4] != 1 or header[5] not in (1, 2):
        raise ValueError("expected an ELF32 firmware executable, not an archive or raw image")
    elf_type, machine = struct.unpack(("<" if header[5] == 1 else ">") + "HH", header[16:20])
    if elf_type != 2 or machine != 40:  # ET_EXEC, EM_ARM
        raise ValueError("expected a linked Arm executable (ET_EXEC/EM_ARM), not a host binary or object")
    result = subprocess.run(
        [nm, "--defined-only", "--format=posix", str(firmware)],
        check=True, capture_output=True, text=True,
    )
    # Undefined/data symbols cannot prove that a driver function was linked.
    functions = {
        fields[0]
        for line in result.stdout.splitlines()
        if len(fields := line.split()) >= 2 and fields[1] in ("T", "t")
    }
    missing = [DRIVER_SYMBOLS[driver] for driver in drivers if DRIVER_SYMBOLS[driver] not in functions]
    if missing:
        raise ValueError("missing linked functions: " + ", ".join(missing))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("drivers", nargs="+", choices=DRIVER_SYMBOLS)
    args = parser.parse_args()
    nm = shutil.which("arm-none-eabi-nm") or shutil.which("llvm-nm")
    if not nm:
        parser.error("install arm-none-eabi-nm or llvm-nm to inspect firmware symbols")
    try:
        verify(args.firmware, args.drivers, nm)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"firmware link check: {error}", file=sys.stderr)
        return 1
    print(f"firmware link verified: {args.firmware} ({', '.join(args.drivers)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
