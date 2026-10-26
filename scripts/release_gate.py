#!/usr/bin/env python3
"""Fail-closed, read-only exact-main publication gate (stdlib + GitHub CLI)."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from urllib.parse import urlencode

REQUIRED_JOBS = {
    "ci.yml": (
        "Host release and API contracts",
        "Vendored TDK subset standalone compile",
        "Nested CMake consumer contract",
        "Module compile smoke (all declared SoCs, Arm GNU)",
        "Real NSX bare-metal and FreeRTOS final links",
    ),
    "zephyr.yml": ("zephyr",),
}
PENDING = {"queued", "in_progress", "waiting", "requested", "pending"}


class GateError(RuntimeError):
    """Missing, ambiguous, unsuccessful or unreadable release evidence."""


class GitHub:
    def __init__(self, repository: str, deadline: float):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise GateError("Expected repository OWNER/REPO")
        self.prefix = f"repos/{repository}/actions"
        self.deadline = deadline

    def api(self, path: str):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise GateError("Timed out waiting for release evidence")
        try:
            result = subprocess.run(
                ["gh", "api", "--paginate", "--slurp", f"{self.prefix}/{path}"],
                check=True, capture_output=True, text=True,
                timeout=min(60, remaining),
            )
            pages = json.loads(result.stdout)
        except (OSError, subprocess.SubprocessError, ValueError) as exc:
            raise GateError(f"Cannot read GitHub evidence: {exc}") from exc
        if not isinstance(pages, list) or not pages:
            raise GateError("GitHub returned no pages")
        return pages

    def collection(self, path: str, key: str, *, capped: bool = False):
        pages = self.api(path)
        items = []
        total = None
        for page in pages:
            if not isinstance(page, dict) or not isinstance(page.get(key), list):
                raise GateError(f"Malformed {key} page")
            count = page.get("total_count")
            if type(count) is not int or count < 0 or (total is not None and count != total):
                raise GateError(f"Inconsistent {key} pagination")
            total = count
            items.extend(page[key])
        if total != len(items) or (capped and total >= 1000):
            raise GateError(f"Incomplete or API-capped {key} evidence")
        ids = [positive_int(item, "id") for item in items]
        if len(set(ids)) != len(ids):
            raise GateError(f"Duplicate {key} evidence")
        return items

    def runs(self, workflow: str, sha: str):
        query = urlencode({"head_sha": sha, "branch": "main", "event": "push", "per_page": 100})
        return self.collection(f"workflows/{workflow}/runs?{query}", "workflow_runs", capped=True)

    def jobs(self, run: dict):
        return self.collection(
            f"runs/{run['id']}/attempts/{run['run_attempt']}/jobs?per_page=100", "jobs"
        )


def positive_int(item, field: str) -> int:
    if not isinstance(item, dict) or type(item.get(field)) is not int or item[field] < 1:
        raise GateError(f"Missing or invalid {field}")
    return item[field]


def latest_run(runs: list, workflow: str, sha: str):
    # Do not trust server-side filters, nor list ordering. PR/dispatch/stale
    # successes must never qualify. run_number orders new runs, run_attempt reruns.
    matches = []
    for run in runs:
        if not isinstance(run, dict):
            raise GateError("Malformed workflow run")
        if (run.get("head_sha"), run.get("head_branch"), run.get("event")) != (sha, "main", "push"):
            continue
        if run.get("path") != f".github/workflows/{workflow}":
            raise GateError("Unexpected workflow path")
        for field in ("id", "run_number", "run_attempt"):
            positive_int(run, field)
        matches.append(run)
    if not matches:
        return None
    numbers = [run["run_number"] for run in matches]
    if len(set(numbers)) != len(numbers):
        raise GateError("Ambiguous workflow runs")
    return max(matches, key=lambda run: run["run_number"])


def signature(run):
    return None if run is None else (
        run["id"], run["run_attempt"], run.get("status"), run.get("conclusion")
    )


def check_once(github, sha: str) -> bool:
    snapshots = {}
    ready = True
    for workflow, required in REQUIRED_JOBS.items():
        run = latest_run(github.runs(workflow, sha), workflow, sha)
        snapshots[workflow] = signature(run)
        if run is None:
            ready = False
            continue
        status = run.get("status")
        if status in PENDING:
            ready = False
            continue
        if status != "completed" or run.get("conclusion") != "success":
            raise GateError(f"{workflow} latest run/attempt did not succeed: {signature(run)}")
        jobs = github.jobs(run)
        for name in required:
            matches = [job for job in jobs if isinstance(job, dict) and job.get("name") == name]
            if len(matches) != 1:
                raise GateError(f"{workflow}: expected exactly one required job: {name}")
            job = matches[0]
            if (job.get("run_id"), job.get("run_attempt"), job.get("status"), job.get("conclusion")) != (
                run["id"], run["run_attempt"], "completed", "success"
            ):
                raise GateError(f"{workflow}: required job did not succeed in current attempt: {name}")
    if not ready:
        return False
    # Recheck both workflows after reading jobs, detecting reruns/new runs that
    # became visible during this check. GitHub does not offer an atomic snapshot.
    for workflow in REQUIRED_JOBS:
        current = latest_run(github.runs(workflow, sha), workflow, sha)
        if signature(current) != snapshots[workflow]:
            return False
    return True


def wait_for_evidence(github, sha: str, deadline: float, *, poll_seconds: float = 30,
                      clock=time.monotonic, sleep=time.sleep):
    while clock() < deadline:
        if check_once(github, sha):
            if clock() >= deadline:
                break
            return
        remaining = deadline - clock()
        if remaining > 0:
            print("Waiting for exact-main CI and Zephyr evidence", flush=True)
            sleep(min(poll_seconds, remaining))
    raise GateError("Timed out waiting for exact-main CI and Zephyr evidence")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--sha", required=True)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"[0-9a-f]{40}", args.sha):
            raise GateError("Expected a full release commit SHA")
        deadline = time.monotonic() + 1800
        wait_for_evidence(GitHub(args.repository, deadline), args.sha, deadline)
    except GateError as exc:
        print(f"Release gate refused publication: {exc}", file=sys.stderr)
        return 1
    print(f"Required CI and Zephyr jobs succeeded for {args.sha}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
