# Publication evidence gate

The manual immutable-release workflow first validates release metadata and that
its checkout is current `origin/main`. It invokes `scripts/release_gate.py` before
the dependent publication job can create a tag or upload an archive. The helper
only reads GitHub Actions evidence; invoking it never publishes anything.

For the exact release commit, both `.github/workflows/ci.yml` and
`.github/workflows/zephyr.yml` must have successful **push / main** runs. PR and
manual-dispatch successes do not qualify. The required CI jobs are:

- Host release and API contracts
- Vendored TDK subset standalone compile
- Nested CMake consumer contract
- Module compile smoke (all declared SoCs, Arm GNU)
- Real NSX bare-metal and FreeRTOS final links

Zephyr additionally requires its `zephyr` job. Every required name must occur
exactly once, completed successfully in the selected run's current attempt.
Missing, duplicate, skipped, cancelled, neutral, failed or stale-attempt job
evidence refuses publication, even if the workflow conclusion says success.

The helper selects the highest workflow run number, independently of response
ordering, and uses that run's current attempt endpoint for jobs. It never falls
back to an older green run or earlier attempt. Pending runs and absent runs can
appear during the bounded wait; completed negative evidence fails immediately.
All API pages are read and their totals and unique IDs checked. Inconsistent or
incomplete pagination, the filtered-runs API's 1,000-result ceiling, malformed
responses, permissions failures and CLI errors fail closed. Each `gh` subprocess
is limited to 60 seconds (or the remaining overall deadline); the gate has a
30-minute overall deadline and polls at most every 30 seconds. The enclosing
validation job has a 40-minute timeout.

Both run selections are rechecked after all jobs have been read to detect reruns
or new runs during validation. GitHub provides no atomic snapshot across workflow
runs or across validation and publication; this check cannot prevent someone
starting another rerun after validation has finished. Treat release-time reruns
as a reason to stop and revalidate publication.

Run the actual gate offline test suite with:

```sh
python3 -m unittest discover -s tests -p 'test_release*.py' -v
```

The tests invoke the production helper's selection, job verification, polling,
pagination and subprocess paths with synthetic GitHub responses. They require no
network, token, SDK or publishing permission. For a read-only live check, use a
GitHub token with Actions read access and the GitHub CLI:

```sh
python3 scripts/release_gate.py --repository AmbiqAI/nsx-sensors --sha FULL_COMMIT_SHA
```

This publication gate establishes software CI evidence, not sensor hardware
qualification or approval of license/provenance obligations.
