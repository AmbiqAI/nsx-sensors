"""Offline behavioral tests for the exact helper invoked before publication."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "release_gate", Path(__file__).parents[1] / "scripts/release_gate.py"
)
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)
SHA = "a" * 40


def run(workflow, **changes):
    value = dict(id=10 if workflow == "ci.yml" else 20, run_number=1,
                 run_attempt=1, head_sha=SHA, head_branch="main", event="push",
                 path=f".github/workflows/{workflow}", status="completed", conclusion="success")
    value.update(changes)
    return value


class Evidence:
    def __init__(self):
        self.records = {workflow: [run(workflow)] for workflow in gate.REQUIRED_JOBS}
        self.job_records = {
            workflow: [dict(id=i + 100, name=name, run_id=self.records[workflow][0]["id"],
                            run_attempt=1, status="completed", conclusion="success")
                       for i, name in enumerate(required)]
            for workflow, required in gate.REQUIRED_JOBS.items()
        }

    def runs(self, workflow, sha):
        return copy.deepcopy(self.records[workflow])

    def jobs(self, record):
        return copy.deepcopy(self.job_records[record["path"].split("/")[-1]])


class GateTests(unittest.TestCase):
    def test_both_workflows_and_all_jobs_pass(self):
        self.assertTrue(gate.check_once(Evidence(), SHA))

    def test_stale_wrong_branch_and_wrong_event_success_do_not_qualify(self):
        for field, value in (("head_sha", "b" * 40), ("head_branch", "feature"),
                             ("event", "pull_request"), ("event", "workflow_dispatch")):
            with self.subTest(field=field, value=value):
                evidence = Evidence()
                evidence.records["zephyr.yml"][0][field] = value
                self.assertFalse(gate.check_once(evidence, SHA))

    def test_every_required_job_is_mandatory_and_unique(self):
        for workflow, required in gate.REQUIRED_JOBS.items():
            for name in required:
                for mutation in ("missing", "duplicate", "failed", "cancelled", "skipped", "neutral",
                                 "stale_attempt", "wrong_run", "running"):
                    with self.subTest(workflow=workflow, name=name, mutation=mutation):
                        evidence = Evidence()
                        jobs = evidence.job_records[workflow]
                        job = next(j for j in jobs if j["name"] == name)
                        if mutation == "missing":
                            jobs.remove(job)
                        elif mutation == "duplicate":
                            jobs.append(copy.deepcopy(job))
                        elif mutation == "stale_attempt":
                            job["run_attempt"] = 2
                        elif mutation == "wrong_run":
                            job["run_id"] = 999
                        elif mutation == "running":
                            job["status"] = "in_progress"
                        else:
                            job["conclusion"] = mutation
                        with self.assertRaises(gate.GateError):
                            gate.check_once(evidence, SHA)

    def test_latest_run_wins_regardless_of_response_order(self):
        for reverse in (False, True):
            evidence = Evidence()
            evidence.records["ci.yml"].append(run("ci.yml", id=11, run_number=2, conclusion="failure"))
            if reverse:
                evidence.records["ci.yml"].reverse()
            with self.assertRaises(gate.GateError):
                gate.check_once(evidence, SHA)

    def test_newer_pending_run_does_not_borrow_older_success(self):
        evidence = Evidence()
        evidence.records["ci.yml"].append(
            run("ci.yml", id=11, run_number=2, status="queued", conclusion=None)
        )
        self.assertFalse(gate.check_once(evidence, SHA))

    def test_latest_rerun_cannot_borrow_prior_attempt_jobs(self):
        evidence = Evidence()
        evidence.records["ci.yml"][0]["run_attempt"] = 2
        with self.assertRaises(gate.GateError):
            gate.check_once(evidence, SHA)
        for job in evidence.job_records["ci.yml"]:
            job["run_attempt"] = 2
        self.assertTrue(gate.check_once(evidence, SHA))

    def test_non_success_workflow_fails_closed(self):
        for conclusion in ("failure", "cancelled", "skipped", "neutral", "timed_out", None):
            evidence = Evidence()
            evidence.records["zephyr.yml"][0]["conclusion"] = conclusion
            with self.assertRaises(gate.GateError):
                gate.check_once(evidence, SHA)

    def test_pending_or_missing_evidence_waits_and_times_out(self):
        for records in ([], [run("zephyr.yml", status="in_progress", conclusion=None)]):
            evidence = Evidence()
            evidence.records["zephyr.yml"] = records
            now = [0]
            def sleep(seconds):
                now[0] += seconds
            with self.assertRaisesRegex(gate.GateError, "Timed out"):
                gate.wait_for_evidence(evidence, SHA, 65, clock=lambda: now[0], sleep=sleep)
            self.assertEqual(now[0], 65)

    def test_wait_can_observe_completion(self):
        evidence = Evidence()
        evidence.records["zephyr.yml"] = []
        def sleep(seconds):
            evidence.records["zephyr.yml"] = [run("zephyr.yml")]
        gate.wait_for_evidence(evidence, SHA, 1, clock=lambda: 0, sleep=sleep)

    def test_rerun_during_job_reads_does_not_pass(self):
        evidence = Evidence()
        original = evidence.jobs
        def jobs(record):
            result = original(record)
            evidence.records["ci.yml"][0]["run_attempt"] = 2
            return result
        evidence.jobs = jobs
        self.assertFalse(gate.check_once(evidence, SHA))

    def test_wrong_workflow_and_malformed_run_fail_closed(self):
        for changes in ({"path": ".github/workflows/other.yml"}, {"run_attempt": None},
                        {"status": "unknown"}, {"run_number": "1"}):
            evidence = Evidence()
            evidence.records["ci.yml"][0].update(changes)
            with self.assertRaises(gate.GateError):
                gate.check_once(evidence, SHA)


class ApiTests(unittest.TestCase):
    def setUp(self):
        self.github = gate.GitHub("AmbiqAI/nsx-sensors", gate.time.monotonic() + 1800)

    def test_actual_paginated_transport_and_attempt_endpoint(self):
        pages = [dict(total_count=101, jobs=[dict(id=i) for i in range(1, 101)]),
                 dict(total_count=101, jobs=[dict(id=101)])]
        with patch.object(gate.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], 0, stdout=json.dumps(pages))) as execute:
            self.assertEqual(len(self.github.jobs(run("ci.yml", run_attempt=3))), 101)
        args, kwargs = execute.call_args
        self.assertIn("--paginate", args[0])
        self.assertIn("--slurp", args[0])
        self.assertIn("/attempts/3/jobs?per_page=100", args[0][-1])
        self.assertLessEqual(kwargs["timeout"], 60)
        self.assertTrue(kwargs["check"])

    def test_latest_run_on_later_page_is_selected(self):
        records = [run("ci.yml", id=i, run_number=i) for i in range(1, 102)]
        records[-1]["conclusion"] = "failure"
        pages = [dict(total_count=101, workflow_runs=records[:100]),
                 dict(total_count=101, workflow_runs=records[100:])]
        with patch.object(gate.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], 0, stdout=json.dumps(pages))):
            selected = gate.latest_run(self.github.runs("ci.yml", SHA), "ci.yml", SHA)
        self.assertEqual(selected["id"], 101)
        self.assertEqual(selected["conclusion"], "failure")

    def test_run_query_is_exact_sha_main_push(self):
        with patch.object(self.github, "api", return_value=[dict(total_count=0, workflow_runs=[])]) as api:
            self.github.runs("ci.yml", SHA)
        path = api.call_args.args[0]
        for query in (f"head_sha={SHA}", "branch=main", "event=push", "per_page=100"):
            self.assertIn(query, path)

    def test_incomplete_duplicate_changing_or_capped_pages_fail(self):
        bad_pages = [[], [{}], [dict(total_count=2, jobs=[dict(id=1)])],
                     [dict(total_count=2, jobs=[dict(id=1), dict(id=1)])],
                     [dict(total_count=2, jobs=[dict(id=1)]), dict(total_count=3, jobs=[dict(id=2)])],
                     [dict(total_count=1000, jobs=[dict(id=i) for i in range(1, 1001)])]]
        for pages in bad_pages:
            with self.subTest(pages=str(pages)[:100]):
                with patch.object(self.github, "api", return_value=pages):
                    with self.assertRaises(gate.GateError):
                        self.github.collection("unused", "jobs", capped=True)

    def test_api_errors_and_malformed_json_fail_closed(self):
        for error in (FileNotFoundError(), subprocess.TimeoutExpired("gh", 60),
                      subprocess.CalledProcessError(1, "gh")):
            with patch.object(gate.subprocess, "run", side_effect=error):
                with self.assertRaises(gate.GateError):
                    self.github.api("unused")
        for stdout in ("not json", "{}", "[]"):
            with patch.object(gate.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout=stdout)):
                with self.assertRaises(gate.GateError):
                    self.github.api("unused")

    def test_deadline_prevents_subprocess(self):
        self.github.deadline = 0
        with patch.object(gate.subprocess, "run") as execute:
            with self.assertRaises(gate.GateError):
                self.github.api("unused")
        execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()
