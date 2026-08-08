# Ownership

Ambiq maintains this repository and owns the release decision for
`nsx-sensors`.

`nsx-sensors` is a device-driver module, not a board-support package and not a
transport. It owns:

- the `nsx_*` public driver APIs in `includes-api/`;
- their implementations in `src/`;
- the vendoring decision for `vendor/tdk-icm45605/`, including which upstream
  subset is carried and which modules are excluded;
- this repository's release metadata and automation.

It does not own, and must not silently change:

- `nsx-core`, `nsx-i2c`, `nsx-spi`, `nsx-soc-hal`, or any other part of
  `AmbiqAI/nsx-ambiq-sdk`;
- AmbiqSuite, CMSIS, or CMSIS-DSP;
- the TDK InvenSense eMD driver, which remains TDK's work under TDK's terms;
- the legacy `AmbiqAI/neuralSPOT` and `AmbiqAI/ns-sensors` drivers these ports
  derive from;
- the stable neuralSPOT-X registry, which is a downstream consumer.

Release qualification requires review from an `nsx-sensors` maintainer and a
successful CI run for the exact commit being tagged. Any change to
`vendor/tdk-icm45605/` additionally requires a licensing review and a
`PROVENANCE.md` update in the same pull request, because the vendored files are
third-party and are not covered by this repository's BSD-3-Clause `LICENSE`.
