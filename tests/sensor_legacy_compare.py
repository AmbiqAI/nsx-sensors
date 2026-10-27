#!/usr/bin/env python3
"""Offline optional comparison against actual pinned pre-migration INA228.

Needs the local git object (no implicit fetch). Shared vectors assert independent
expected values before comparing old/new output; agreement alone is insufficient.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from sensor_mutations import ROOT, must_run

BASE = "65e8f2141801eedac5052d4d533be51ef44cac5f"


def compare(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="nsx-legacy-ina-") as tmp:
        work = Path(tmp)
        for old, name in (("src/nsx_ina228.c", "old.c"),
                          ("includes-api/nsx_ina228.h", "nsx_ina228.h")):
            result = subprocess.run(["git", "show", f"{BASE}:{old}"], cwd=ROOT,
                                    capture_output=True, timeout=30)
            if result.returncode:
                raise RuntimeError(f"pinned historical object {BASE} unavailable; use a full local clone")
            (work / name).write_bytes(result.stdout)
        (work / "nsx_i2c.h").write_text('''#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } nsx_i2c_config_t;
uint32_t nsx_i2c_write(nsx_i2c_config_t *, const void *, uint32_t, uint16_t);
uint32_t nsx_i2c_write_read(nsx_i2c_config_t *, uint16_t, const void *, size_t, void *, size_t);
''')
        common = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                  str(ROOT / "tests/regression/ina228_vectors.c")]
        must_run(common + ["-DLEGACY", "-I", str(work), str(work / "old.c"),
                           "-o", str(work / "old")], work, output / "old-build.log")
        must_run(common + ["-I", str(ROOT / "include"), str(ROOT / "src/common/bus.c"),
                           str(ROOT / "src/ina228/ina228.c"), "-lm", "-o", str(work / "new")],
                 work, output / "new-build.log")
        for name in ("old", "new"):
            must_run([str(work / name)], work, output / f"{name}-vectors.log")
        if (output / "old-vectors.log").read_bytes() != (output / "new-vectors.log").read_bytes():
            raise RuntimeError("legacy/current outputs differ; inspect vector logs")
        (output / "comparison.txt").write_text(f"Historical INA228 {BASE}\n"
            "Both passed independent expectations; all 22 vector results agree.\n")
        print((output / "comparison.txt").read_text(), end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    compare(parser.parse_args().output.resolve())
