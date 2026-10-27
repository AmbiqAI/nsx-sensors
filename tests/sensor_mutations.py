#!/usr/bin/env python3
"""Bounded, offline behavioral negative controls; never edits the checkout.

A compiling mutant must fail its executable with an assertion/check diagnostic.
Timeouts, missing anchors, configure/build errors and crashes without a test
assertion are infrastructure errors, never evidence of defect detection.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

# Explicit edits, not a mutation framework. Expected behavior lives in the C
# tests and docs/sensor-simulation.md, not in this list of deliberate defects.
MUTATIONS = (
    ("bme-field-address", "bme688", "src/bme688/bme688.c",
     "#define REG_FIELD0 0x1d", "#define REG_FIELD0 0x1e"),
    ("led-command", "bus_ledstick", "src/ledstick/ledstick.c",
     "#define CMD_ALL_COLORS 0x72", "#define CMD_ALL_COLORS 0x71"),
    ("ina-ready-historical", "ina228", "src/ina228/ina228.c",
     "INA228_REG_DIAGALRT, &value, 1, 1)", "INA228_REG_DIAGALRT, &value, 1, 0)"),
    ("ina-calibration-historical", "ina228", "src/ina228/ina228.c",
     "(range ? 4.0 : 1.0)", "(4.0 * range)"),
    ("mpu-range-historical", "mpu6050", "src/mpu6050/mpu6050.c",
     "GYRO_CONFIG, 0x18, (uint8_t)(range << 3)", "GYRO_CONFIG, 0x18, (uint8_t)(range << 4)"),
    ("max-fifo-address", "max86150", "src/max86150/max86150.c",
     "RD=6, DATA=7,", "RD=6, DATA=6,"),
    ("icm-spi-read-bit", "icm45605", "src/icm45605/icm45605.c",
     "uint8_t address=reg|0x80;", "uint8_t address=reg;"),
    ("tsl-byte-order", "tsl254x", "src/tsl254x/tsl254x.c",
     "(uint16_t)data[1] << 8 | data[0]", "(uint16_t)data[0] << 8 | data[1]"),
    ("t5838-stop-width", "t5838", "src/t5838/t5838.c",
     "result.high_cycles[25] = 130;", "result.high_cycles[25] = 128;"),
)
ROOT = Path(__file__).resolve().parents[1]


def command(args: list[str], cwd: Path, log: Path, timeout: int = 120) -> subprocess.CompletedProcess:
    result = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=timeout)
    log.write_text(result.stdout)
    return result


def must_run(args: list[str], cwd: Path, log: Path) -> None:
    result = command(args, cwd, log)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); see {log}")


def copy_inputs(destination: Path) -> dict[str, str]:
    hashes = {}
    for name in ("CMakeLists.txt", "version.txt", "cmake", "src", "include", "vendor", "tests", "examples"):
        source = ROOT / name
        if source.is_dir():
            shutil.copytree(source, destination / name,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        else:
            shutil.copy2(source, destination / name)
        for path in ([source] if source.is_file() else sorted(source.rglob("*"))):
            if path.is_file() and "__pycache__" not in path.parts:
                hashes[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return hashes


def campaign(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    report = {"source_revision": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_status": subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=ROOT, text=True), "results": []}
    # No baseline checkout or source mutation in the user's worktree. Use the
    # production CMake source lists, then rebuild only each changed target.
    with tempfile.TemporaryDirectory(prefix="nsx-sensor-mutations-") as tmp:
        source = Path(tmp) / "source"
        source.mkdir()
        report["input_sha256"] = copy_inputs(source)
        build = Path(tmp) / "build"
        must_run(["cmake", "-S", str(source), "-B", str(build),
                  "-DNSX_SENSORS_TESTS=ON", "-DCMAKE_BUILD_TYPE=Debug"], source, output / "configure.log")
        must_run(["cmake", "--build", str(build), "--parallel", "2"], source, output / "build.log")
        must_run(["ctest", "--test-dir", str(build), "--output-on-failure"], source, output / "control.log")
        for name, test, filename, before, after in MUTATIONS:
            path = source / filename
            original = path.read_text()
            if original.count(before) != 1:
                raise RuntimeError(f"{name}: stale/nonunique anchor")
            path.write_text(original.replace(before, after))
            try:
                must_run(["cmake", "--build", str(build), "--target", f"test_{test}"],
                         source, output / f"{name}-build.log")
                result = command([str(build / "tests" / "portable" / f"test_{test}")],
                                 build, output / f"{name}-test.log", timeout=10)
                assertion = "Assertion" in result.stdout and "failed" in result.stdout
                check = "FAIL " in result.stdout  # INA228's explicit CHECK diagnostics
                detected = result.returncode != 0 and (assertion or check)
                report["results"].append({"mutation": name, "test": test,
                    "returncode": result.returncode, "detected": detected})
                if not detected:
                    raise RuntimeError(f"{name}: survived or failed without a test diagnostic")
            finally:
                path.write_text(original)
                must_run(["cmake", "--build", str(build), "--target", f"test_{test}"],
                         source, output / f"{name}-restore-build.log")
                must_run([str(build / "tests" / "portable" / f"test_{test}")],
                         build, output / f"{name}-restore-test.log")
            print(f"{name}: detected; restored control passed", flush=True)
        must_run(["ctest", "--test-dir", str(build), "--output-on-failure"], source, output / "final-control.log")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="new directory for evidence")
    args = parser.parse_args()
    campaign(args.output.resolve())
