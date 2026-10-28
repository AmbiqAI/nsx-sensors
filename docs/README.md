# Documentation guide

Maintained guidance describes supported use; dated records describe what was
reviewed or measured at a particular revision. A merged proposal or passing
host test does not establish release publication or hardware qualification.

## Maintained guidance

| Topic | Start here |
| --- | --- |
| Supported devices and limitations | [Catalog](../CATALOG.md), [compatibility](compatibility.md) |
| Public source contract | [v1 API contract](v1-api-contract.md), public headers in `include/nsx/sensors/` |
| Framework integration | [CMake](cmake-integration.md), [NSX](nsx-port.md), [Zephyr](zephyr.md) |
| Device-specific use | [BME688](bme688.md), [TSL254x](tsl254x.md), [T5838](t5838.md) |
| Migration from v0 | [Portable foundation](migration/portable-foundation.md), [API cleanup](migration/api-cleanup.md); device guides are linked from the [root README](../README.md#usage) |
| Validation procedures and limits | [Release](release-validation.md), [NSX firmware](firmware-validation.md), [Zephyr](zephyr-validation.md), [simulation](sensor-simulation.md), [hardware](hardware-validation.md) |
| Release process and ownership | [Release policy](../RELEASE.md), [owners](../OWNERS.md), [provenance](../PROVENANCE.md) |

## Decisions and bounded records

- [RFC 0001](rfcs/0001-portable-sensor-driver-architecture.md) records the accepted
  architectural decision. The linked source contract and public headers define
  supported API use; proposal sketches are not a second API specification.
- [v1 readiness audit](v1-readiness.md) and
  [technical migration review](migration/release-review.md) retain their stated
  baselines. They are historical evidence, not continuously renewed audits.
- [Resource qualification](resource-qualification.md) explains how to reproduce
  the pinned measurements in `resources/`. Preserve revision, toolchain and
  hash attribution; later metadata changes do not renew measurements.
- [v1.0.0 release checklist](releases/v1.0.0.md) tracks that release only.
  Complete it with explicit approvals and exact-SHA evidence; after publication,
  retain it as a closed record. General release instructions belong in RELEASE.md.
- `proposals/` contains retained implementation acceptance scopes, labeled with
  their closing PR and resulting guide. Their original constraints are historical,
  not current task instructions. Routine new proposals belong in PR descriptions.

## Maintenance rules

Ambiq's module maintainers own this documentation under [OWNERS.md](../OWNERS.md).
The author of a behavior change updates the affected maintained guide in the same
PR; reviewers check that guide against implementation and public headers.

1. Extend an existing guide first. Add a file only for a durable topic with a
   clear audience, a link from this index or its parent guide, and a reason it
   cannot fit there. Keep task plans, review transcripts and temporary reports
   in PRs or local artifacts. Do not create one documentation file per PR.
2. Give each rule or procedure one authoritative home. Link to it from summaries
   instead of copying API inventories, test counts, release status or instructions.
3. Put durable architectural decisions in RFCs with status and decision reference.
   When closing a proposal, mark it implemented, superseded or withdrawn and link
   the outcome. Update user guidance rather than leaving it embedded in a proposal.
4. Date evidence and identify its exact revision and limits. Correct a record
   transparently; do not silently rewrite historical findings as current results.
   Store raw evidence only when a retained report needs it for reproduction.
5. During release review, check links, stale proposal statuses and affected guides.
   Merge duplicate guidance and remove obsolete material once needed context is
   preserved in a decision or git history; repair inbound links in the same PR.
   Retain migration guidance while supported consumers still need that transition.
