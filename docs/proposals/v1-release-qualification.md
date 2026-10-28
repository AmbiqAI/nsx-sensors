# v1 release qualification and regression protection

Status: implemented by [PR #33](https://github.com/AmbiqAI/nsx-sensors/pull/33).
Historical acceptance scope; see the [resulting guidance](../release-validation.md) for supported
use or release prerequisites. Original scope and authorization limits below apply
to that proposal, not subsequent work.

Original baseline: merged PR32, f3b313a.
No release, version bump, registry repin, hardware flashing or sensor-driver
behavior change is included.

## Outcomes and acceptance

1. Publication requires both CI and Zephyr workflows on the exact release SHA,
   push event/main branch, and all required jobs (including real NSX firmware).
   Fail closed on failed/cancelled/skipped/missing evidence; bound waiting and
   subprocesses. Offline tests exercise actual gate logic for negative and
   positive cases, including stale/wrong-event evidence and latest reruns.
2. Reproducible resource evidence for the eight cores and adapters: context and
   scratch sizes, representative linked flash/RAM, and compiler stack-usage on
   named target/toolchain/optimization. Reuse production CMake target/source
   definitions. State inclusion/retention, library costs and vendor/callback paths;
   static frames/call analysis are not measured worst-case runtime high water.
   Preserve raw machine-readable results and rerunnable commands.
3. Reconcile NOTICE/PROVENANCE/CATALOG/release and migration documentation with
   current paths, eight-core scope, frozen source contract and historical evidence.
   Preserve third-party license text and attribution. Review migration guidance
   and source mappings; clearly identify maintainer approval still required by
   OWNERS rather than manufacturing legal/provenance signoff.
4. Protect existing drivers: retain unchanged production implementation and public
   contracts; run existing per-driver host/sanitizer and installed-consumer checks,
   relevant NSX/Zephyr integration, plus tests for added release/resource tooling.
   Verify driver/header/vendor diffs are empty. Investigate regressions before
   proposing behavioral changes; any necessary driver fix must be separately
   identified and reviewed.
5. Draft hardware validation plan uses available sensor/board inventory when
   supplied. Start with non-destructive identity/configuration/readback and
   baseline repeatability, then device-specific acquisition/error/shared-bus
   checks. Record firmware/SDK/board/sensor revisions and preserve known-good
   application behavior. Do not assume hardware availability or run hardware.

## Completion workflow

Implement, independently review against this proposal, run local Copilot and CI,
address findings, then present final judgment for maintainer approval. Audit25
records exact evidence and remaining release/hardware gates. Resource reports
with explicit static-analysis limits do not establish electrical qualification.
