#!/usr/bin/env python3
"""Explicit offline final links against one immutable, caller-provided SDK."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

from firmware_link_map import parse_map
from verify_firmware_link import DRIVER_SYMBOLS, verify

SDK_REVISION = "40b893d859672caf0b17b0e51db351a77b423e56"
ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str], *, cwd: Path = ROOT, timeout: int = 120) -> str:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise ValueError(f"{shlex.join(command)} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_firmware(out: Path, mode: str) -> dict[str, object]:
    elf, link_map = out / "firmware.elf", out / "firmware.map"
    verify(elf, list(DRIVER_SYMBOLS), "arm-none-eabi-nm", link_map)
    symbols = {}
    for line in run(["arm-none-eabi-nm", "--defined-only", "--format=posix", str(elf)]).splitlines():
        fields = line.split()
        if len(fields) >= 3:
            symbols[fields[0]] = (fields[1], int(fields[2], 16))
    required = {
        "Reset_Handler": ("libnsx_startup.a", "startup_gcc.c"),
        "SystemCoreClockUpdate": ("libnsx_startup.a", "system_apollo4p.c"),
        "nsx_i2c_write_read_combined": ("libnsx_i2c.a", "nsx_i2c.c"),
        "nsx_i2c_read": ("libnsx_i2c.a", "nsx_i2c.c"),
        "nsx_i2c_write": ("libnsx_i2c.a", "nsx_i2c.c"),
        "nsx_spi_read": ("libnsx_spi.a", "nsx_spi.c"),
        "nsx_spi_write": ("libnsx_spi.a", "nsx_spi.c"),
        "am_bsp_iom_pins_enable": ("libam_bsp.a", "am_bsp"),
        "am_hal_iom_blocking_transfer": ("libam_hal.a", "am_hal_iom"),
        "nsx_delay_us": ("libnsx_runtime.a", "nsx_core_portable.c"),
        "am_util_delay_us": ("libnsx_ambiq_hal.a", "am_util_delay.c"),
    }
    if mode == "freertos":
        required.update({symbol: ("libnsx_freertos.a", member) for symbol, member in {
            "nsx_freertos_start": "nsx_freertos.c",
            "vTaskStartScheduler": "tasks.c", "xTaskCreate": "tasks.c",
            "xQueueSemaphoreTake": "queue.c", "xQueueGenericSend": "queue.c",
            "xPortStartScheduler": "port.c", "vPortSVCHandler": "port.c",
            "xPortPendSVHandler": "port.c", "xPortSysTickHandler": "port.c",
            "SVC_Handler": "nsx_freertos_handlers_cm4f.c",
            "PendSV_Handler": "nsx_freertos_handlers_cm4f.c",
            "SysTick_Handler": "nsx_freertos_handlers_cm4f.c",
        }.items()})
        if symbols.get("sensor_task", (None,))[0] != "t":
            raise ValueError("missing application task entry")
    elif any(name in symbols for name in ("vTaskStartScheduler", "xPortStartScheduler")):
        raise ValueError("bare-metal firmware unexpectedly contains the kernel")
    evidence = parse_map(link_map.read_text())
    for symbol, (archive, member) in required.items():
        kind, address = symbols.get(symbol, (None, 0))
        if kind != "T":
            raise ValueError(f"missing strong implementation: {symbol}")
        evidence.verify(symbol, address, archive, member)
    # Verify actual vector bytes, not just the presence of handler symbols.
    binary = out / "firmware.bin"
    run(["arm-none-eabi-objcopy", "-O", "binary", str(elf), str(binary)])
    vector_address = symbols["g_am_pfnVectors"][1]
    # This declared board's linker places the vector table at FLASH origin.
    if vector_address != 0x18000:
        raise ValueError("unexpected apollo4p_evb vector table address")
    vectors = struct.unpack_from("<16I", binary.read_bytes())
    slots = {1: "Reset_Handler"}
    if mode == "freertos":
        slots.update({11: "SVC_Handler", 14: "PendSV_Handler", 15: "SysTick_Handler"})
    for slot, name in slots.items():
        if vectors[slot] != symbols[name][1] | 1:
            raise ValueError(f"vector {slot} does not route to {name}")
    commands = json.loads((out / "compile_commands.json").read_text())
    def profile(command: str) -> list[str]:
        return sorted(flag for flag in shlex.split(command)
                      if flag.startswith(("-mcpu=", "-mfpu=", "-mfloat-abi="))
                      or flag in ("-mthumb", "-fshort-enums", "-fshort-wchar"))
    sdk_profile = next(profile(entry["command"]) for entry in commands
                       if entry["file"].endswith("/nsx_i2c.c"))
    if not all(any(flag.startswith(prefix) for flag in sdk_profile)
               for prefix in ("-mcpu=", "-mfpu=", "-mfloat-abi=")):
        raise ValueError("SDK compilation profile lacks CPU/FPU/ABI settings")
    for entry in commands:
        source = Path(entry["file"])
        relevant = any(source.is_relative_to(ROOT / part) for part in ("src", "vendor", "tests"))
        relevant = relevant or "/modules/nsx-freertos/" in str(source)
        if relevant and profile(entry["command"]) != sdk_profile:
            raise ValueError(f"compilation profile differs from SDK: {entry['file']}")
    return {"sdk_compile_profile": sdk_profile, "symbols": required, "sha256": {
        name: digest(out / name) for name in ("firmware.elf", "firmware.map", "firmware.bin")
    }}


def check_rejections(out: Path) -> list[str]:
    """Perturb real link artifacts; each failure must identify the intended gate."""
    checked = []
    for fault, expected in (("weak-handler", "missing strong implementation: SVC_Handler"),
                            ("misrouted-vector", "vector 11 does not route"),
                            ("wrong-profile", "compilation profile differs")):
        with tempfile.TemporaryDirectory(prefix="nsx-link-negative-") as directory:
            trial = Path(directory)
            for name in ("firmware.elf", "firmware.map", "compile_commands.json"):
                shutil.copyfile(out / name, trial / name)
            elf = trial / "firmware.elf"
            if fault == "weak-handler":
                run(["arm-none-eabi-objcopy", "--weaken-symbol=SVC_Handler", str(elf)])
            elif fault == "misrouted-vector":
                section = trial / "text.bin"
                run(["arm-none-eabi-objcopy", "--dump-section", f".text={section}", str(elf)])
                data = bytearray(section.read_bytes())
                struct.pack_into("<I", data, 11 * 4, 0)
                section.write_bytes(data)
                run(["arm-none-eabi-objcopy", "--update-section", f".text={section}", str(elf)])
            else:
                path = trial / "compile_commands.json"
                commands = json.loads(path.read_text())
                entry = next(entry for entry in commands
                             if entry["file"] == str(ROOT / "tests/nsx_firmware/main.c"))
                entry["command"] = entry["command"].replace("-mfloat-abi=hard", "-mfloat-abi=soft")
                path.write_text(json.dumps(commands))
            try:
                check_firmware(trial, "freertos")
            except ValueError as error:
                if expected not in str(error):
                    raise ValueError(f"negative {fault} failed for the wrong reason: {error}") from error
            else:
                raise ValueError(f"negative {fault} was accepted")
            checked.append(fault)
    return checked


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path, help=f"clean SDK checkout at {SDK_REVISION}")
    parser.add_argument("--output", type=Path, default=ROOT / "build/firmware-integration",
                        help="new or empty evidence directory (existing builds are rejected)")
    args = parser.parse_args()
    try:
        sdk, output = args.sdk.resolve(), args.output.resolve()
        for tool in ("git", "cmake", "arm-none-eabi-gcc", "arm-none-eabi-nm", "arm-none-eabi-objcopy"):
            if not shutil.which(tool):
                raise ValueError(f"required tool not found: {tool}")
        if not sdk.is_dir():
            raise ValueError(f"SDK directory does not exist: {sdk}")
        if run(["git", "rev-parse", "HEAD"], cwd=sdk).strip() != SDK_REVISION:
            raise ValueError(f"SDK must be pinned to {SDK_REVISION}")
        if run(["git", "status", "--porcelain", "--untracked-files=all"], cwd=sdk).strip():
            raise ValueError("SDK checkout must be clean, including untracked files")
        if output.exists() and any(output.iterdir()):
            raise ValueError(f"output must be new or empty: {output}")
        output.mkdir(parents=True, exist_ok=True)
        record = {
            "sdk_revision": SDK_REVISION, "sdk_path": str(sdk), "board": "apollo4p_evb",
            "sensor_revision": run(["git", "rev-parse", "HEAD"]).strip(),
            "sensor_status": run(["git", "status", "--porcelain"]),
            "sensor_diff_sha256": hashlib.sha256(run(["git", "diff", "HEAD"]).encode()).hexdigest(),
            "compiler": run(["arm-none-eabi-gcc", "--version"]),
            "compiler_path": shutil.which("arm-none-eabi-gcc"),
            "fixture_sha256": {str(p.relative_to(ROOT)): digest(p)
                               for p in sorted((ROOT / "tests/nsx_firmware").glob("*")) if p.is_file()},
            "cmake": run(["cmake", "--version"]), "modes": {},
        }
        for mode in ("baremetal", "freertos"):
            out = output / mode
            out.mkdir()
            commands = [
                ["cmake", "-S", str(ROOT / "tests/nsx_firmware"), "-B", str(out),
                 f"-DCMAKE_TOOLCHAIN_FILE={ROOT}/tests/nsx_firmware/toolchain.cmake",
                 f"-DNSX_SDK_ROOT={sdk}", f"-DFIRMWARE_MODE={mode}",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
                ["cmake", "--build", str(out), "--parallel", "2"],
            ]
            (out / "commands.txt").write_text("\n".join(map(shlex.join, commands)) + "\n")
            for phase, command in zip(("configure", "build"), commands):
                with (out / f"{phase}.log").open("w") as log:
                    subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                   check=True, timeout=600)
            record["modes"][mode] = check_firmware(out, mode)
            inputs = [Path(p) for p in (out / "sdk-inputs.txt").read_text().splitlines() if p]
            record["modes"][mode]["sdk_input_sha256"] = {str(p): digest(p) for p in inputs}
            print(f"Verified {mode}: {out / 'firmware.elf'}", flush=True)
        record["negative_checks"] = check_rejections(output / "freertos")
        (output / "evidence.json").write_text(json.dumps(record, indent=2) + "\n")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"firmware integration: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
