Prepare the v1.0.0 software release candidate from merged audit baseline `c0276068a6b2dee8ae0adaa6d1d2c7f9dc86e26f`.

Acceptance:
- Align version.txt, nsx-module.yaml, CMake and current catalog/docs at 1.0.0; clearly label publication pending.
- Consolidate unreleased work into accurate v1 release notes, preserving prior release history and migration guidance.
- Preserve production APIs/implementations, vendor/licensing text and platform declarations.
- Add a concise maintainer checklist distinguishing software qualification, provenance approval, repository controls, exact merged-main CI and separately authorized publication/registry promotion.
- Run portable/sanitizer/package tests, host contracts and all hosted integration checks; obtain independent and local Copilot review, address findings, then present the PR.

No merge, tag/release, registry repin, hardware execution or repository-settings mutation is authorized by this preparation. Hardware qualification remains explicitly pending. Existing resource evidence retains its measured revision rather than being relabeled as a measurement of new metadata.
