#!/usr/bin/env bash
# Build actual library targets with framework-owned ABI settings, then relocate
# their package and verify a separately compiled C++ caller uses the same layout.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
for mode in auto explicit; do
    explicit=OFF; flag=-fshort-enums
    if [[ "$mode" == explicit ]]; then explicit=ON; flag=-fno-short-enums; fi
    build="$out/$mode"
    cmake -S "$root/tests/compile_context" -B "$build" \
        -DSENSOR_SOURCE="$root" -DEXPLICIT_CONTEXT="$explicit" -DENUM_FLAG="$flag" \
        -DCMAKE_INSTALL_PREFIX="$build/install" -DCMAKE_INSTALL_LIBDIR=lib
    cmake --build "$build" --parallel
    "$build/context_probe"
    "$build/port_probe"
    # Inspect every actual core/vendor/adapter compilation, not another source list.
    python3 - "$build/compile_commands.json" "$flag" <<'PY'
import json, sys
from pathlib import Path
entries = json.loads(Path(sys.argv[1]).read_text())
assert entries
for entry in entries:
    command = entry['command']
    assert sys.argv[2] in command, entry['file']
    assert 'SENSOR_CONTEXT_PROBE=1' in command, entry['file']
PY
    cmake --install "$build"
    mv "$build/install" "$build/relocated"
    cmake -S "$root/tests/compile_context" -B "$build/consumer" \
        -DCMAKE_PREFIX_PATH="$build/relocated" -DEXPLICIT_CONTEXT="$explicit" -DENUM_FLAG="$flag"
    cmake --build "$build/consumer" --parallel
    "$build/consumer/context_probe"
    if cmake -S "$root/tests/compile_context" -B "$build/missing" \
        -DCMAKE_PREFIX_PATH="$build/relocated" -DOMIT_CONTEXT=ON > "$build/missing.log" 2>&1; then
        echo 'Installed package accepted a missing compilation context' >&2
        exit 1
    fi
    grep -q 'Load compilation-context target' "$build/missing.log"
done
if cmake -S "$root" -B "$out/invalid" -DNSX_SENSOR_COMPILE_CONTEXT=missing::profile \
    > "$out/invalid.log" 2>&1; then
    echo 'Source build accepted an unknown compilation context' >&2
    exit 1
fi
grep -q "NSX_SENSOR_COMPILE_CONTEXT target 'missing::profile'" "$out/invalid.log"
