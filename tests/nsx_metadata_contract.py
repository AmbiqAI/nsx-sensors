"""Run with the pinned neuralspotx consumer installed (see CI)."""
from copy import deepcopy
from pathlib import Path

from neuralspotx.metadata import load_yaml, validate_nsx_module_metadata

ROOT = Path(__file__).resolve().parents[1]
metadata = load_yaml(ROOT / "nsx-module.yaml")
validate_nsx_module_metadata(metadata, "nsx-module.yaml")

# The schema validates types; verify the advertised integration exists too.
integration = metadata["integrations"]["zephyr"]
assert (ROOT / integration["path"]).is_dir()
for field in ("module_yml", "kconfig"):
    assert (ROOT / integration[field]).is_file(), field
zephyr = load_yaml(ROOT / integration["module_yml"])
assert zephyr["build"]["cmake"] == integration["path"]
assert zephyr["build"]["kconfig"] == integration["kconfig"]

# Negative control: reproduce the actual pre-fix consumer failure.
broken = deepcopy(metadata)
del broken["integrations"]
try:
    validate_nsx_module_metadata(broken, "missing-integration.yaml")
except ValueError as error:
    assert "integrations" in str(error), str(error)
else:
    raise AssertionError("NSX accepted the original missing-integration defect")
print("NSX metadata, integration paths, and negative control passed")
