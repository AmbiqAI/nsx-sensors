"""Fast host-side contract tests for the nsx-sensors release foundation.

These tests deliberately avoid NSX, AmbiqSuite, and cross-toolchain
dependencies so they run on any CI runner. Target builds live in
``tests/configure_target_smoke.sh`` because the SDK supplies the board and
toolchain graph, and the vendored TDK subset has its own standalone compile
smoke in ``tests/vendor_compile_smoke.sh``.
"""

from __future__ import annotations

import io
import os
import tempfile
import textwrap
import re
import subprocess
import tarfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
API = ROOT / "includes-api"
SRC = ROOT / "src"
VENDOR = ROOT / "vendor/tdk-icm45605"

SEMVER = r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$"

# The exact TDK eMD "common files" + "basic driver" subset recorded in
# PROVENANCE.md. Anything else under vendor/ is an unreviewed intake.
VENDOR_FILES = {
    "inv_imu.h",
    "inv_imu_defs.h",
    "inv_imu_driver.c",
    "inv_imu_driver.h",
    "inv_imu_regmap_be.h",
    "inv_imu_regmap_le.h",
    "inv_imu_transport.c",
    "inv_imu_transport.h",
    "inv_imu_version.h",
}
VENDOR_COMPILED = {"inv_imu_driver.c", "inv_imu_transport.c"}
VENDOR_EXCLUDED = {
    "inv_imu_driver_advanced.c",
    "inv_imu_driver_advanced.h",
    "inv_imu_edmp.c",
    "inv_imu_edmp.h",
    "inv_imu_edmp_defs.h",
    "inv_imu_edmp_memmap.h",
    "inv_imu_selftest.c",
    "inv_imu_selftest.h",
}

PUBLIC_HEADERS = ()


def module_version() -> str:
    text = (ROOT / "nsx-module.yaml").read_text()
    match = re.search(r'(?m)^  version:\s*"([^"]+)"\s*$', text)
    if not match:
        raise AssertionError("missing module version in nsx-module.yaml")
    return match.group(1)


def tracked_files() -> list[str]:
    return subprocess.check_output(
        ["git", "ls-files"], cwd=ROOT, text=True
    ).splitlines()



def top_level_trigger_keys(workflow: str) -> set[str]:
    """Return the keys under a workflow's top-level ``on:`` mapping.

    Hand-rolled so the test suite keeps running on a bare stdlib Python: CI
    runners are not guaranteed to have PyYAML. Only the block structure this
    repository's workflows use is supported, which is enough to assert the
    complete trigger set instead of the absence of one literal spelling.
    """
    lines = workflow.splitlines()
    try:
        start = next(i for i, line in enumerate(lines) if line.rstrip() == "on:")
    except StopIteration:
        raise AssertionError("workflow has no top-level 'on:' block")
    keys: set[str] = set()
    for line in lines[start + 1 :]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip())
        if indent == 0:
            break
        if indent == 2:
            key = line.strip().split(":", 1)[0].lstrip("- ").strip()
            if key:
                keys.add(key)
    if not keys:
        raise AssertionError("workflow 'on:' block has no triggers")
    return keys


class ReleaseMetadataTests(unittest.TestCase):
    def test_versions_are_coherent_semver(self) -> None:
        version = (ROOT / "version.txt").read_text().strip()
        self.assertRegex(version, SEMVER)
        self.assertEqual(version, module_version())
        self.assertIn(
            f"project(nsx_sensors VERSION {version} LANGUAGES C)",
            (ROOT / "CMakeLists.txt").read_text(),
        )

    def test_release_foundation_files_exist(self) -> None:
        for name in (
            "CATALOG.md",
            "CHANGELOG.md",
            "LICENSE",
            "NOTICE",
            "OWNERS.md",
            "PROVENANCE.md",
            "README.md",
            "RELEASE.md",
            "version.txt",
            "docs/compatibility.md",
        ):
            self.assertTrue((ROOT / name).is_file(), name)

    def test_changelog_documents_the_released_version(self) -> None:
        version = (ROOT / "version.txt").read_text().strip()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        self.assertIn(f"## [{version}]", changelog)
        self.assertRegex(changelog, r"(?m)^## \[\d+\.\d+\.\d+\] - \d{4}-\d{2}-\d{2}$")


class LicensingTests(unittest.TestCase):
    def test_ambiq_code_is_bsd_3_clause(self) -> None:
        license_text = (ROOT / "LICENSE").read_text()
        self.assertIn("BSD 3-Clause License", license_text)
        self.assertIn("Ambiq", license_text)

    def test_notice_retains_invensense_terms_without_relicensing(self) -> None:
        notice = (ROOT / "NOTICE").read_text()
        self.assertIn("Copyright (c) [2020] by InvenSense, Inc.", notice)
        self.assertIn(
            "Permission to use, copy, modify, and/or distribute this software for any",
            notice,
        )
        self.assertIn("NOT relicensed under", notice)
        self.assertIn("vendor/tdk-icm45605", notice)

    def test_every_vendored_file_retains_its_upstream_notice(self) -> None:
        for path in sorted(VENDOR.iterdir()):
            if path.suffix not in {".c", ".h"}:
                continue
            head = path.read_text(errors="replace")[:1200]
            self.assertIn("InvenSense, Inc.", head, path.name)
            self.assertIn("Permission to use, copy, modify", head, path.name)


class ProvenanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.provenance = (ROOT / "PROVENANCE.md").read_text()

    def test_records_audited_baseline_and_dependency_pins(self) -> None:
        self.assertIn("9a73d590fee7b377011af0b998a7563571acc228", self.provenance)
        self.assertIn("a9f4ec25a162f6f3700623feb691423bb5a51132", self.provenance)

    def test_records_neuralspot_port_lineage_with_exact_revisions(self) -> None:
        self.assertIn("AmbiqAI/neuralSPOT", self.provenance)
        self.assertIn("AmbiqAI/ns-sensors", self.provenance)
        for revision in (
            "4264b9309e03064ffad13a0468d5d0c1110c5288",
            "69c58a11b8746dd6b72a0d6e7b2e796e1c1dd9c8",
            "a59b545ec6d630f10c1af0a7f25bb4d8457cc5c8",
            "68a60afd713e0555fcd4f9322bc34ce29c7e984b",
        ):
            self.assertIn(revision, self.provenance)
        for source in ("src/nsx_icm45605.c", "src/nsx_ina228.c", "src/nsx_ledstick.c"):
            name = Path(source).name
            self.assertIn(name, self.provenance, name)

    def test_records_vendor_version_source_subset_and_exclusions(self) -> None:
        self.assertIn("2.1.0", self.provenance)
        self.assertIn("7a12440477411aa28aa732047d4dd33a6a3c8a73", self.provenance)
        self.assertIn("extern/drivers/tdk/icm45605/imu/", self.provenance)
        for name in VENDOR_FILES:
            self.assertIn(name, self.provenance, name)
        for name in VENDOR_EXCLUDED:
            self.assertIn(name, self.provenance, name)
        self.assertIn("imu/", self.provenance)

    def test_declared_vendor_version_matches_the_vendored_header(self) -> None:
        header = (VENDOR / "inv_imu_version.h").read_text()
        match = re.search(r'INV_IMU_VERSION_STRING\s+"([^"]+)"', header)
        assert match is not None
        self.assertIn(f"`{match.group(1)}`", self.provenance)

    def test_records_the_ina228_float_compatibility_change(self) -> None:
        self.assertIn("float32_t", self.provenance)
        self.assertIn("arm_math.h", self.provenance)


class VendorSubsetTests(unittest.TestCase):
    def test_vendor_directory_matches_the_documented_subset(self) -> None:
        present = {path.name for path in VENDOR.iterdir() if path.is_file()}
        self.assertEqual(VENDOR_FILES, present)

    def test_excluded_upstream_modules_are_absent(self) -> None:
        present = {path.name for path in VENDOR.iterdir() if path.is_file()}
        self.assertEqual(set(), VENDOR_EXCLUDED & present)

    def test_vendor_includes_have_no_upstream_imu_prefix(self) -> None:
        for path in sorted(VENDOR.iterdir()):
            if path.suffix not in {".c", ".h"}:
                continue
            self.assertNotIn('#include "imu/', path.read_text(errors="replace"))

    def test_no_cmsis_dsp_dependency_is_reintroduced(self) -> None:
        paths = [*API.rglob("*.h"), *SRC.rglob("*.c"), *SRC.rglob("*.h"),
                 *(ROOT / "include").rglob("*.h")]
        for path in paths:
            self.assertNotIn("arm_math.h", path.read_text(), path.name)
            self.assertNotIn("float32_t", path.read_text(), path.name)


class CMakeContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.cmake = (ROOT / "CMakeLists.txt").read_text()

    def test_target_alias_export_name_and_export_set(self) -> None:
        self.assertIn("add_library(nsx_sensors INTERFACE", self.cmake)
        self.assertIn("add_library(nsx::sensors ALIAS nsx_sensors)", self.cmake)
        self.assertIn(
            "set_target_properties(nsx_sensors PROPERTIES EXPORT_NAME sensors)",
            self.cmake,
        )
        self.assertIn("list(APPEND nsx_portable_targets nsx_sensors)", self.cmake)
        self.assertNotIn("nsx_sensorsTargets", self.cmake)

    def test_gnuinstalldirs_is_included_before_it_is_used(self) -> None:
        self.assertIn("include(GNUInstallDirs)", self.cmake)
        self.assertLess(
            self.cmake.index("include(GNUInstallDirs)"),
            self.cmake.index("${CMAKE_INSTALL_INCLUDEDIR}"),
        )

    def test_project_is_guarded_to_a_top_level_configure(self) -> None:
        guard = "if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)"
        self.assertIn(guard, self.cmake)
        self.assertLess(
            self.cmake.index(guard), self.cmake.index("project(nsx_sensors")
        )
        self.assertIn(
            f"project(nsx_sensors VERSION {module_version()} LANGUAGES C)", self.cmake
        )
        # The module has no C++ or assembly sources; enabling them would force
        # needless compiler detection.
        self.assertNotIn("LANGUAGES C CXX ASM", self.cmake)

    def test_dependencies_are_guarded(self) -> None:
        for dependency in ("nsx::core", "nsx::i2c", "nsx::spi"):
            self.assertIn(dependency, self.cmake, dependency)
        self.assertIn("FATAL_ERROR", self.cmake)
        self.assertIn("if(NOT TARGET ${dependency})", self.cmake)

    def test_sources_are_explicit_not_globbed(self) -> None:
        self.assertNotIn("file(GLOB", self.cmake)
        for name in VENDOR_COMPILED:
            self.assertIn(f"vendor/tdk-icm45605/{name}", self.cmake, name)
        for name in VENDOR_FILES - VENDOR_COMPILED:
            if name.endswith(".c"):
                self.fail(f"uncompiled vendor source {name}")

    def test_file_ends_with_a_newline(self) -> None:
        self.assertTrue(self.cmake.endswith(")\n"))


class PublicApiTests(unittest.TestCase):
    def test_headers_use_non_reserved_include_guards(self) -> None:
        headers = [API / name for name in PUBLIC_HEADERS]
        headers.extend((ROOT / "include/nsx/sensors").glob("*.h"))
        for path in headers:
            name = path.name
            text = path.read_text()
            guards = re.findall(r"(?m)^#(?:ifndef|define)\s+(\w+)$", text)
            self.assertTrue(guards, name)
            for guard in guards:
                self.assertFalse(
                    guard.startswith("__") or guard.startswith("_"),
                    f"{name} uses reserved guard {guard}",
                )

    def test_icm45605_frame_buffering_api_is_absent(self) -> None:
        header = (ROOT / "include/nsx/sensors/icm45605.h").read_text()
        source = (SRC / "icm45605/icm45605.c").read_text()
        for symbol in (
            "frame_buffer",
            "frame_available_cb",
            "frame_size",
            "icm45605_frame_available_cb",
        ):
            self.assertNotIn(symbol, header, symbol)
            self.assertNotIn(symbol, source, symbol)

    def test_icm45605_interrupt_opt_in_is_explicit(self) -> None:
        header = (ROOT / "include/nsx/sensors/icm45605.h").read_text()
        source = (SRC / "icm45605/icm45605.c").read_text()
        self.assertIn("drdy_interrupt", header)
        self.assertIn("icm45605_set_drdy_interrupt", source)

    def test_icm45605_per_instance_contract_is_documented(self) -> None:
        header = (ROOT / "include/nsx/sensors/icm45605.h").read_text()
        self.assertIn("address-stable context", header)
        self.assertIn("Not ISR-safe", header)
        self.assertIn("inv_imu_device_t _device", header)
        source = (SRC / "icm45605/icm45605.c").read_text()
        self.assertNotIn("static inv_imu_device_t", source)
        self.assertNotIn("icm45605_handle_interrupt(void)", source)


class CompatibilityDeclarationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.metadata = (ROOT / "nsx-module.yaml").read_text()
        self.compatibility = (ROOT / "docs/compatibility.md").read_text()

    def test_required_dependencies_match_the_cmake_link_interface(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for module, target in (
            ("nsx-core", "nsx::core"),
            ("nsx-i2c", "nsx::i2c"),
            ("nsx-spi", "nsx::spi"),
        ):
            self.assertIn(f"- {module}", self.metadata, module)
            self.assertIn(target, cmake, target)

    def test_declared_socs_do_not_exceed_dependency_evidence(self) -> None:
        block = re.search(
            r"(?ms)^compatibility:\n(?:.*?)^  socs:\n((?:    - [^\n]*\n)+)", self.metadata
        )
        assert block is not None
        declared = {line.strip("- \n").strip('"') for line in block.group(1).splitlines()}
        # Intersection of nsx-core, nsx-i2c, and nsx-spi at the audited SDK pin.
        self.assertEqual(
            {
                "apollo3",
                "apollo3p",
                "apollo4l",
                "apollo4p",
                "apollo330P",
                "apollo510",
                "apollo510b",
                "apollo510L",
            },
            declared,
        )
        self.assertNotIn("apollo5b", declared)
        self.assertNotIn("apollo2", declared)
        self.assertNotIn("atomiq110", declared)

    def test_apollo5b_exclusion_records_evidence_and_the_gate(self) -> None:
        self.assertIn("apollo5b", self.compatibility)
        self.assertIn("descriptor-only", self.compatibility)
        self.assertIn("configure-ready", self.compatibility)

    def test_hardware_validation_is_not_claimed(self) -> None:
        self.assertIn(
            "No driver in this release is hardware-in-the-loop qualified",
            self.compatibility,
        )
        self.assertIn("Runtime validated", self.compatibility)
        self.assertIn("release status", (ROOT / "README.md").read_text().lower())

    def test_smokes_never_skip_a_missing_toolchain(self) -> None:
        for name in ("configure_target_smoke.sh", "module_compile_smoke.sh"):
            smoke = (ROOT / "tests" / name).read_text()
            self.assertIn("set -euo pipefail", smoke, name)
            for tool in ("arm-none-eabi-gcc", "armclang", "atfe"):
                self.assertIn(tool, smoke, f"{name}: {tool}")
            # A missing toolchain must be a hard failure, never a silent pass.
            self.assertNotRegex(smoke, r"(?m)^\s*exit 0\s*(#.*)?$", name)
            self.assertIn("exit 1", smoke, name)
            # An ATfE request must be verified, not assumed.
            self.assertIn("Arm Toolchain ID:", smoke, name)
            self.assertIn("InstalledDir", smoke, name)

    def test_build_verification_table_matches_the_declared_socs(self) -> None:
        block = re.search(
            r"(?ms)^compatibility:\n(?:.*?)^  socs:\n((?:    - [^\n]*\n)+)", self.metadata
        )
        assert block is not None
        declared = {line.strip("- \n").strip('"') for line in block.group(1).splitlines()}
        for soc in declared:
            self.assertIn(f"`{soc}`", self.compatibility, soc)
        for toolchain in ("arm-none-eabi-gcc", "armclang", "ATfE"):
            self.assertIn(toolchain, self.compatibility, toolchain)


class ReleaseAutomationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ci = (ROOT / ".github/workflows/ci.yml").read_text()
        self.release = (ROOT / ".github/workflows/release.yml").read_text()

    def test_all_third_party_actions_are_pinned_to_a_commit_sha(self) -> None:
        for name, workflow in (("ci", self.ci), ("release", self.release)):
            uses = re.findall(r"(?m)^\s*(?:- )?uses:\s*(\S+)\s*$", workflow)
            self.assertTrue(uses, name)
            for ref in uses:
                self.assertRegex(ref, r"@[0-9a-f]{40}$", f"{name}: {ref} is unpinned")

    def test_release_is_manual_and_gated_on_exact_commit_ci(self) -> None:
        # Publication must never be a side effect of a push, so assert the whole
        # trigger set rather than the absence of one literal spelling: a `push:`
        # added after `workflow_dispatch:` would not contain "on:\n  push:".
        self.assertEqual({"workflow_dispatch"}, top_level_trigger_keys(self.release))
        self.assertIn("python3 scripts/release_gate.py", self.release)
        self.assertIn('--sha "$RELEASE_SHA"', self.release)
        gate = (ROOT / "scripts/release_gate.py").read_text()
        for workflow in ("ci.yml", "zephyr.yml"):
            self.assertIn(workflow, gate)
        self.assertIn("Refusing to retarget", self.release)
        for job in (
            "Host release and API contracts",
            "Vendored TDK subset standalone compile",
            "Nested CMake consumer contract",
            "Module compile smoke (all declared SoCs, Arm GNU)",
        ):
            self.assertIn(job, gate)

    def test_ci_runs_on_push_and_pull_request(self) -> None:
        self.assertEqual(
            {"push", "pull_request", "workflow_dispatch"},
            top_level_trigger_keys(self.ci),
        )

    def test_publish_gate_failure_prevents_tag_mutation(self) -> None:
        publish = self.release.split('\n  publish:', 1)[1]
        self.assertIn('actions: read', publish)
        step = publish.split('- name: Create the annotated tag', 1)[1].split('\n      - name:', 1)[0]
        self.assertIn('GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}', step)
        self.assertIn('RELEASE_SHA: ${{ needs.validate.outputs.sha }}', step)
        script = textwrap.dedent(step.split('        run: |\n', 1)[1])
        self.assertLess(script.index('python3 scripts/release_gate.py'), script.index('git tag -a'))
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            # Execute the actual workflow shell with bounded fake executables.
            # A failed publication-side gate must stop before tag lookup/write.
            (directory / 'git').write_text('#!/bin/sh\necho "$*" >> "$TRACE"\n'
                                          'if [ "$1" = rev-parse ]; then echo "$RELEASE_SHA"; fi\n')
            (directory / 'python3').write_text('#!/bin/sh\necho "gate $*" >> "$TRACE"\nexit 1\n')
            for name in ('git', 'python3'):
                (directory / name).chmod(0o755)
            env = dict(os.environ, PATH=tmp + os.pathsep + os.environ['PATH'],
                       TRACE=str(directory / 'trace'), RELEASE_SHA='a' * 40,
                       RELEASE_TAG='v0.3.0', GITHUB_REPOSITORY='test/sensors')
            result = subprocess.run(['bash', '-c', script], env=env, capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            lines = (directory / 'trace').read_text().splitlines()
            self.assertEqual(lines, ['rev-parse HEAD',
                'gate scripts/release_gate.py --repository test/sensors --sha ' + 'a' * 40])

    def test_publication_requires_release_environment(self) -> None:
        validate, publish = self.release.split('\n  publish:', 1)
        self.assertNotRegex(validate, r"(?m)^    environment:")
        self.assertRegex(publish, r"(?m)^    environment: release$")
        self.assertIn('    needs: validate\n', publish)

    def test_publication_rechecks_main_after_approval_and_ci(self) -> None:
        publish = self.release.split('\n  publish:', 1)[1]
        step = publish.split('- name: Create the annotated tag', 1)[1].split('\n      - name:', 1)[0]
        script = textwrap.dedent(step.split('        run: |\n', 1)[1])
        # Execute the actual shell: stale or unreadable main must stop before
        # tag lookup or mutation, while a matching main permits new publication.
        for scenario in ('advanced', 'fetch_failed', 'ref_failed', 'matching'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                (directory / 'git').write_text(
                    '#!/bin/sh\necho "git $*" >> "$TRACE"\n'
                    'case "$1" in\n'
                    '  rev-parse)\n'
                    '    if [ "$2" = origin/main ]; then\n'
                    '      [ "$SCENARIO" = ref_failed ] && exit 1\n'
                    '      if [ "$SCENARIO" = advanced ]; then echo other; exit 0; fi\n'
                    '    fi\n'
                    '    echo "$RELEASE_SHA" ;;\n'
                    '  fetch) [ "$SCENARIO" != fetch_failed ] ;;\n'
                    '  ls-remote) exit 2 ;;\n'
                    'esac\n')
                (directory / 'python3').write_text(
                    '#!/bin/sh\necho "gate $*" >> "$TRACE"\nexit 0\n')
                for name in ('git', 'python3'):
                    (directory / name).chmod(0o755)
                env = dict(os.environ, PATH=tmp + os.pathsep + os.environ['PATH'],
                           TRACE=str(directory / 'trace'), RELEASE_SHA='a' * 40,
                           RELEASE_TAG='v1.0.0', GITHUB_REPOSITORY='test/sensors',
                           SCENARIO=scenario)
                result = subprocess.run(['bash', '-c', script], env=env,
                                        capture_output=True, timeout=10)
                lines = (directory / 'trace').read_text().splitlines()
                self.assertEqual(lines[:3], [
                    'git rev-parse HEAD',
                    'gate scripts/release_gate.py --repository test/sensors --sha ' + 'a' * 40,
                    'git fetch --no-tags origin main:refs/remotes/origin/main'])
                if scenario == 'matching':
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('git tag -a v1.0.0 -m Release v1.0.0 ' + 'a' * 40, lines)
                    self.assertEqual(lines[-1], 'git push origin v1.0.0')
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse(any(line.startswith(('git ls-remote', 'git tag', 'git push'))
                                         for line in lines), lines)

    def test_release_creates_an_immutable_annotated_tag_with_checksum(self) -> None:
        self.assertIn("git tag -a", self.release)
        self.assertIn("git archive --format=tar.gz", self.release)
        self.assertIn("sha256sum", self.release)
        self.assertIn("retry_existing_tag", self.release)
        self.assertIn('refs/tags/${tag}^{commit}', self.release)
        self.assertIn("tar -tzf \"$archive\" > archive.list", self.release)
        self.assertNotIn('tar -tzf "$archive" | grep -q', self.release)

    def test_ci_runs_host_contracts_and_vendor_compile_smoke(self) -> None:
        self.assertIn("unittest discover", self.ci)
        self.assertIn("tests/vendor_compile_smoke.sh", self.ci)
        self.assertIn("tests/nested_contract", self.ci)
        self.assertIn("tests/module_compile_smoke.sh", self.ci)
        self.assertIn("runs-on: ubuntu-latest", self.ci)
        self.assertIn("repository: AmbiqAI/nsx-ambiq-sdk", self.ci)
        self.assertIn("ref: 1f18f01555897ed8e06437ff9727f2bb61771e70", self.ci)
        self.assertIn("1f18f01555897ed8e06437ff9727f2bb61771e70", self.ci)
        self.assertIn("gcc-arm-none-eabi", self.ci)
        self.assertIn("libnewlib-arm-none-eabi", self.ci)
        self.assertNotIn("NSX_SENSORS_MODULE_SMOKE", self.ci)
        self.assertNotIn("NSX_SENSORS_TARGET_SMOKE", self.ci)

    def test_module_compile_matrix_is_warning_clean(self) -> None:
        smoke = (ROOT / "tests/module_compile_smoke.sh").read_text()
        self.assertIn("-Wall -Wextra -Werror", smoke)


class NestedContractHarnessTests(unittest.TestCase):
    """The harness must really exercise the nesting contract, not assert trivia."""

    def setUp(self) -> None:
        self.harness = ROOT / "tests/nested_contract/CMakeLists.txt"
        self.assertTrue(self.harness.is_file())
        self.text = self.harness.read_text()

    def test_harness_checks_the_subdirectory_project_scope(self) -> None:
        # PROJECT_NAME only changes inside the subdirectory's own scope, so a
        # top-level check would silently pass an unguarded project().
        self.assertIn("get_directory_property(nsx_sensors_project", self.text)
        self.assertIn("DEFINITION PROJECT_NAME", self.text)

    def test_harness_checks_export_name_alias_and_portable_aggregate(self) -> None:
        self.assertIn("EXPORT_NAME", self.text)
        self.assertIn("nsx::sensors", self.text)
        self.assertIn("INTERFACE_LINK_LIBRARIES", self.text)
        self.assertIn("nsx_sensor_icm45605", self.text)


class SourceArchiveTests(unittest.TestCase):
    def test_archive_carries_the_release_contract_and_no_build_output(self) -> None:
        required = {
            "LICENSE",
            "NOTICE",
            "PROVENANCE.md",
            "CHANGELOG.md",
            "RELEASE.md",
            "version.txt",
        }
        tracked = set(tracked_files())
        missing = {name for name in required if name not in tracked}
        if missing:
            # Keep the test useful before the release-foundation commit lands.
            self.assertTrue(
                all((ROOT / name).is_file() for name in missing), missing
            )
            return
        archive = subprocess.check_output(
            ["git", "archive", "--format=tar", "HEAD"], cwd=ROOT
        )
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as tar:
            names = {member.name for member in tar.getmembers()}
        self.assertTrue(required.issubset(names), required - names)
        self.assertFalse({name for name in names if name.startswith(".git/")})
        self.assertFalse({name for name in names if name.startswith("build/")})


if __name__ == "__main__":
    unittest.main()
