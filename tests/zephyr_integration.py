#!/usr/bin/env python3
"""Explicit Linux Zephyr provision/build/run gate; never called by host tests."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

from firmware_link_map import parse_map

ZEPHYR_SHA = "684c9e8f32e4373a21098559f748f06915f950c9"
PROJECTS = {
    "cmsis": ("modules/hal/cmsis", "512cc7e895e8491696b61f7ba8066b4a182569b8"),
    "cmsis_6": ("modules/hal/cmsis_6", "30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74"),
}
ROOT = Path(__file__).resolve().parents[1]


class Runner:
    def __init__(self, workspace: Path, timeout: int):
        self.workspace = workspace
        self.timeout = timeout
        self.evidence = workspace / "evidence"
        self.evidence.mkdir(parents=True, exist_ok=True)
        (self.evidence / "result.json").unlink(missing_ok=True)
        self.commands: list[dict] = []
        self.env = os.environ.copy()
        # Never inherit a caller's unrelated SDK/workspace selection.
        for key in ("ZEPHYR_BASE", "ZEPHYR_TOOLCHAIN_VARIANT", "CROSS_COMPILE",
                    "ZEPHYR_SDK_INSTALL_DIR", "ZEPHYR_MODULES", "ZEPHYR_EXTRA_MODULES"):
            self.env.pop(key, None)
        self.env["PATH"] = str(workspace / "venv/bin") + os.pathsep + self.env["PATH"]
        self.env["ZEPHYR_BASE"] = str(workspace / "zephyr")

    def run(self, *args: str | Path, env: dict | None = None, timeout: int | None = None) -> str:
        command = [str(arg) for arg in args]
        print("+ " + " ".join(command), flush=True)
        log = self.evidence / f"{len(self.commands):02d}.log"
        record = {"command": command, "log": log.name}
        self.commands.append(record)
        started = time.monotonic()
        with log.open("w") as output:
            process = subprocess.Popen(command, cwd=self.workspace, env=env or self.env,
                                       stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=timeout or self.timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                record["timeout"] = True
                raise RuntimeError(f"command timed out; see {log}") from None
            finally:
                record["seconds"] = round(time.monotonic() - started, 3)
                record["returncode"] = process.returncode
                (self.evidence / "commands.json").write_text(json.dumps(self.commands, indent=2) + "\n")
        text = log.read_text()
        if code:
            print(text[-12000:], file=sys.stderr)
            raise RuntimeError(f"command failed ({code}); see {log}")
        return text.strip()

    def verify(self, path: Path, revision: str) -> None:
        actual = self.run("git", "-C", path, "rev-parse", "HEAD")
        if actual != revision:
            raise RuntimeError(f"{path}: expected {revision}, found {actual}; use a new workspace")
        if self.run("git", "-C", path, "status", "--porcelain", "--untracked-files=no"):
            raise RuntimeError(f"{path} has tracked changes; provision a clean workspace")

    def provision(self) -> None:
        zephyr = self.workspace / "zephyr"
        if not zephyr.exists():
            self.run("git", "init", zephyr)
            self.run("git", "-C", zephyr, "remote", "add", "origin",
                     "https://github.com/zephyrproject-rtos/zephyr.git")
            self.run("git", "-C", zephyr, "fetch", "--depth=1", "origin", ZEPHYR_SHA)
            self.run("git", "-C", zephyr, "checkout", "--detach", ZEPHYR_SHA)
        self.verify(zephyr, ZEPHYR_SHA)
        python = self.workspace / "venv/bin/python"
        if not python.exists():
            self.run(sys.executable, "-m", "venv", self.workspace / "venv")
        self.run(python, "-m", "pip", "install", "west==1.5.0", "cmake==3.31.6", "ninja==1.11.1.4",
                 "-r", zephyr / "scripts/requirements-base.txt")
        west = self.workspace / "venv/bin/west"
        if not (self.workspace / ".west").exists():
            self.run(west, "init", "-l", zephyr)
        self.verify_manifest()
        for path, revision in PROJECTS.values():
            checkout = self.workspace / path
            if checkout.exists():
                self.verify(checkout, revision)
        self.run(west, "update", "--narrow", *PROJECTS)

    def verify_manifest(self) -> None:
        path = self.run(self.workspace / "venv/bin/west", "manifest", "--path")
        if Path(path).resolve() != self.workspace / "zephyr/west.yml":
            raise RuntimeError("workspace west configuration must use its pinned zephyr/west.yml")

    def build(self, label: str, board: str, cross: str | None = None, app: str = "zephyr") -> Path:
        build = self.workspace / ("build-" + label)
        env = self.env.copy()
        env["ZEPHYR_TOOLCHAIN_VARIANT"] = "cross-compile" if cross else "host"
        args = []
        if cross:
            env["CROSS_COMPILE"] = cross
            # The provided GNU Arm toolchain ships newlib. Zephyr's generic
            # cross-compile mode cannot infer this from the prefix alone.
            args.append("-DTOOLCHAIN_HAS_NEWLIB=ON")
        modules = [str(self.workspace / path) for path, _ in PROJECTS.values()]
        modules.append(str(ROOT))
        self.run("cmake", "--fresh", "-GNinja", "-S", ROOT / "tests" / app, "-B", build,
                 f"-DBOARD={board}", "-DZEPHYR_MODULES=" + ";".join(modules), *args, env=env)
        self.run("cmake", "--build", build, "--parallel", "4", env=env)
        return build


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True,
                        help="isolated writable provision/build directory, outside this repository")
    parser.add_argument("--provision", action="store_true", help="explicitly allow network dependency setup")
    parser.add_argument("--arm-prefix", help="GNU Arm compiler prefix, e.g. /opt/arm/bin/arm-none-eabi-")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per provision/build command")
    args = parser.parse_args()
    workspace = args.workspace.expanduser().resolve()
    if workspace == ROOT or ROOT in workspace.parents:
        parser.error("workspace must be outside the source repository")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if sys.version_info < (3, 12):
        parser.error("Zephyr 4.4.0 needs Python 3.12 or newer")
    workspace.mkdir(parents=True, exist_ok=True)
    runner = Runner(workspace, args.timeout)
    try:
        if args.provision:
            runner.provision()
        runner.verify(workspace / "zephyr", ZEPHYR_SHA)
        for path, revision in PROJECTS.values():
            runner.verify(workspace / path, revision)
        runner.verify_manifest()
        # Assert our minimal project pin list still agrees with this exact manifest.
        manifest = runner.run(workspace / "venv/bin/west", "list", *PROJECTS,
                              "-f", "{name} {revision}")
        for name, (_, revision) in PROJECTS.items():
            if f"{name} {revision}" not in manifest.splitlines():
                raise RuntimeError(f"manifest pin differs for {name}")
        compiler = shutil.which("arm-none-eabi-gcc")
        cross = args.arm_prefix or (compiler.removesuffix("gcc") if compiler else None)
        if not cross:
            raise RuntimeError("install a GNU Arm toolchain with newlib or pass --arm-prefix")
        versions = {}
        for tool in ("cmake", "ninja", "gcc", cross + "gcc"):
            versions[tool] = runner.run(tool, "--version")
        versions["python"] = runner.run(workspace / "venv/bin/python", "--version")
        versions["packages"] = runner.run(workspace / "venv/bin/python", "-m", "pip", "freeze")
        versions["source_revision"] = runner.run("git", "-C", ROOT, "rev-parse", "HEAD")
        versions["source_status"] = runner.run("git", "-C", ROOT, "status", "--porcelain")
        versions["zephyr"] = ZEPHYR_SHA
        versions["manifest_projects"] = PROJECTS
        (runner.evidence / "versions.json").write_text(json.dumps(versions, indent=2) + "\n")
        native = runner.build("native", "native_sim/native/64")
        result = runner.run(native / "zephyr/zephyr.exe", timeout=60)
        print(result)
        if "PROJECT EXECUTION SUCCESSFUL" not in result or "pass = 3, fail = 0" not in result:
            raise RuntimeError("native ztest success summary missing")
        arm = runner.build("arm", "qemu_cortex_m3", cross)
        firmware = arm / "zephyr/zephyr.elf"
        link_map = arm / "zephyr/zephyr.map"
        # Make the selected compiler's sibling nm visible to the shared CLI.
        runner.env["PATH"] = str(Path(cross).parent) + os.pathsep + runner.env["PATH"]
        # Reuse the ELF32/EM_ARM/code-symbol/archive-provenance verifier.
        runner.run(workspace / "venv/bin/python", ROOT / "tests/verify_firmware_link.py",
                   firmware, "--map", link_map, "bme688")
        symbols = runner.run(cross + "nm", "--defined-only", "--format=posix", firmware)
        functions = {
            fields[0]: int(fields[2], 16)
            for line in symbols.splitlines()
            if len(fields := line.split()) >= 3 and fields[1] in ("T", "t")
        }
        evidence = parse_map(link_map.read_text())
        for symbol in ("nsx_sensor_zephyr_bind_i2c", "nsx_sensor_zephyr_bind_spi"):
            if symbol not in functions:
                raise RuntimeError(f"Arm ELF missing code symbol: {symbol}")
            evidence.verify(symbol, functions[symbol], "libnsx_sensor_port_zephyr.a", "zephyr.c")
        # GNU maps omit local function symbol rows, but retain their allocated
        # input section. Require the static wrapper function's code address to
        # start in the real wrapper object's unique allocated section.
        symbol = "nsx_bme688_fetch"
        if symbol not in functions:
            raise RuntimeError("Arm ELF missing wrapper fetch code")
        address = functions[symbol] & ~1
        owners = [section for section in evidence.sections
                  if section.address <= address < section.address + section.size]
        if (len(owners) != 1 or owners[0].address != address or
                not owners[0].origin.endswith("libnsx_sensor_port_zephyr.a(bme688.c.obj)")):
            raise RuntimeError("wrapper fetch code does not originate in its allocated archive member")
        runner.build("t5838", "native_sim/native/64", app="zephyr_t5838")
        (runner.evidence / "result.json").write_text(json.dumps({
            "native": "3 tests passed", "arm": "qemu_cortex_m3 linked (not executed)",
            "t5838": "native_sim control-only build, I2C/SPI disabled", "hardware": "not tested",
            "arm_elf_sha256": hashlib.sha256((arm / "zephyr/zephyr.elf").read_bytes()).hexdigest(),
        }, indent=2) + "\n")
        print(f"Zephyr integration passed; evidence: {runner.evidence}")
        return 0
    except (RuntimeError, OSError, ValueError) as error:
        print(f"Zephyr integration failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
