# Bounded sensor behavior regression qualification

Status: implemented by [PR #34](https://github.com/AmbiqAI/nsx-sensors/pull/34).
Historical acceptance scope; see the [resulting guidance](../sensor-simulation.md) for supported
use or release prerequisites. Original scope and authorization limits below apply
to that proposal, not subsequent work.

## Problem and scope

Hardware is unavailable. Strengthen the existing host tests without treating
current driver output as an oracle or building a general-purpose simulator.
Cover all eight cores (TSL2540/2541 share one), reusing existing scenarios where
already adequate. Keep production sources and public contracts unchanged unless
a separately explained defect is demonstrated.

## Acceptance

1. Add a small number of independently sourced device behaviors where existing
   models are weak. Virtual time must advance device events independently of
   whether the driver polls. Model tests must exercise those semantics directly.
2. Extend before/after-side-effect fault injection where it adds coverage. Assert
   output/state/recovery contracts, not a complete incidental transaction order.
3. Record a per-core regression evidence map, including preserved behavior and
   intentional migration corrections. Run historical code only for a bounded,
   comparable interface; otherwise explicitly distinguish reconstructed historical
   defects from an actual old/new execution comparison.
4. Run a bounded mutation campaign in disposable source copies: clean controls
   must pass, each mutation must compile, and the relevant test must reject it.
   Compile failures/timeouts do not count as detected behavioral defects. Include
   at least one case per core and representative historical corrections.

Expected values must come from named datasheet sections, independently generated
reference vectors, or explicit NSX contracts. Do not derive them from production
constants. Record limitations and surviving mutations rather than weakening tests
or expanding scope indefinitely. No synthetic physical accuracy, bus electrical,
clock waveform or hardware qualification claims.

## Delivery

Draft PR, implementation, focused host/sanitizer validation, independent review,
Copilot/CI, findings addressed, then final judgment for maintainer approval.
No merge, release, hardware flashing or new runtime dependencies.
