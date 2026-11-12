#!/usr/bin/env python3
"""Repeatable, independently graded Agent productivity experiments (stdlib only)."""

from __future__ import annotations

import argparse
import hashlib
from collections import Counter, defaultdict
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import shutil
import subprocess
import sys
import tempfile

from common import digest, fingerprint, read_json, run_process, usage_metrics, write_json

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
FIXTURE_FILES = ("main.nut", "config.nut", "game.agent.json")


def load_suite(path):
    suite = read_json(path)
    if suite.get("schema") != "eve.agent-benchmark-suite/v1":
        raise ValueError("unsupported suite schema")
    for name, expected in suite["inputTextSha256"].items():
        actual = hashlib.sha256(
            (REPO / name).read_text(encoding="utf-8").encode("utf-8")
        ).hexdigest()
        if actual != expected:
            raise ValueError(f"pinned input drift: {name}; review inputs and version the suite")
    identifiers = [t["id"] for t in suite["tasks"]]
    if any(not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,80}", identifier) for identifier in identifiers):
        raise ValueError("invalid task id")
    if len(set(identifiers)) != len(identifiers):
        raise ValueError("duplicate task ids")
    for task in suite["tasks"]:
        if not task["allowedFiles"] or not set(task["allowedFiles"]) <= set(FIXTURE_FILES):
            raise ValueError("invalid allowedFiles")
        if task["grader"] not in ("contract", "engine"):
            raise ValueError("unknown grader")
        if task["mutation"]["file"] not in FIXTURE_FILES:
            raise ValueError("invalid mutation file")
        if not math.isfinite(task["timeoutSeconds"]) or task["timeoutSeconds"] <= 0:
            raise ValueError("invalid timeout")
    return suite


def prepare(suite, task, workspace):
    workspace.mkdir()
    for name in FIXTURE_FILES:
        shutil.copyfile(REPO / suite["fixture"] / name, workspace / name)
    mutation = task["mutation"]
    path = workspace / mutation["file"]
    source = path.read_text(encoding="utf-8")
    if source.count(mutation["old"]) != 1:
        raise ValueError(f"fixture drift for {task['id']}; review and version the suite")
    path.write_text(source.replace(mutation["old"], mutation["new"]), encoding="utf-8")
    # Every contestant receives the same small knowledge pack, with its hash recorded.
    shutil.copyfile(REPO / ".cursor/skills/evescript/SKILL.md", workspace / "EVESCRIPT.md")
    return fingerprint(workspace)


def changed_files(before, after):
    return sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))


def wilson(passed, total):
    if not total:
        return None
    z = 1.959963984540054
    rate = passed / total
    denominator = 1 + z * z / total
    center = (rate + z * z / (2 * total)) / denominator
    radius = z * math.sqrt(rate * (1 - rate) / total + z * z / (4 * total * total)) / denominator
    return [max(0, center - radius), min(1, center + radius)]


def quantile(values, q):
    if not values:
        return None
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * q) - 1)]


def summarize(trials):
    groups = defaultdict(list)
    for trial in trials:
        groups[trial["taskId"]].append(trial)
    tasks = {}
    for identifier, rows in sorted(groups.items()):
        passed = sum(row["status"] == "pass" for row in rows)
        tasks[identifier] = {
            "trials": len(rows),
            "passed": passed,
            "successRate": passed / len(rows),
            "wilson95": wilson(passed, len(rows)),
            "statuses": dict(Counter(row["status"] for row in rows)),
            "agentWallP50": quantile([r["agent"]["wallSeconds"] for r in rows], 0.5),
            "agentWallP95": quantile([r["agent"]["wallSeconds"] for r in rows], 0.95),
        }
    # Unknown telemetry is not zero. Include failed attempts in all totals.
    usage = {}
    for key in ("inputTokens", "outputTokens", "costUsd", "toolCalls", "humanInterventions"):
        values = [t.get("usage", {}).get(key) for t in trials]
        usage[key] = {
            "reportedTrials": sum(v is not None for v in values),
            "total": sum(values) if values and all(v is not None for v in values) else None,
        }
    passes = sum(t["status"] == "pass" for t in trials)
    return {
        "tasks": tasks,
        "macroSuccessRate": sum(t["successRate"] for t in tasks.values()) / len(tasks)
        if tasks
        else None,
        "passed": passes,
        "trials": len(trials),
        "usage": usage,
        "agentSecondsPerSuccess": sum(t["agent"]["wallSeconds"] for t in trials) / passes
        if passes
        else None,
        "costUsdPerSuccess": usage["costUsd"]["total"] / passes
        if passes and usage["costUsd"]["total"] is not None
        else None,
        "rankable": bool(trials)
        and not any(t["status"] in ("blocked", "grader_error", "adapter_error") for t in trials),
    }


def identity(suite, args):
    files = {name: digest(REPO / suite["fixture"] / name) for name in FIXTURE_FILES}
    files["EVESCRIPT.md"] = digest(REPO / ".cursor/skills/evescript/SKILL.md")
    return {
        "suite": suite,
        "inputs": files,
        "harness": {p.name: digest(p) for p in sorted(HERE.glob("*.py"))},
        "engineSha256": digest(args.engine) if args.engine else None,
        "environmentLabel": args.environment_label,
        "cpuCount": os.cpu_count(),
        "processor": platform.processor(),
        "renderEnvironment": {
            key: os.environ.get(key)
            for key in ("VK_ICD_FILENAMES", "SDL_VIDEODRIVER", "ALSOFT_DRIVERS")
        },
        "platform": platform.platform(),
        "python": platform.python_version(),
        "graderTimeoutSeconds": args.grader_timeout,
        "contextPolicy": "fixture+evescript-pack/v1",
    }


def trial_run(suite, task, seed, repeat, args, directory):
    directory.mkdir()
    workspace = directory / "workspace"
    before = prepare(suite, task, workspace)
    request = {
        "schema": "eve.agent-benchmark-request/v1",
        "taskId": task["id"],
        "prompt": task["prompt"],
        "workspace": str(workspace),
        "seed": seed,
        "repeat": repeat,
        "allowedFiles": task["allowedFiles"],
        "timeoutSeconds": task["timeoutSeconds"],
        "artifacts": str(directory / "adapter-artifacts"),
        "engine": str(args.engine) if args.engine else None,
        "rules": "Only edit allowedFiles. Read EVESCRIPT.md. No human help. Do not edit evaluation infrastructure. Final prose is not proof.",
    }
    (directory / "adapter-artifacts").mkdir()
    write_json(directory / "request.json", request)
    write_json(directory / "initial-files.json", before)
    row = {
        "taskId": task["id"],
        "category": task["category"],
        "seed": seed,
        "repeat": repeat,
        "status": "blocked",
        "agent": {"wallSeconds": 0},
        "usage": {},
        "checks": [],
    }
    if task["grader"] == "engine" and not args.engine:
        row["reason"] = "engine executable not configured; no simulated score"
        write_json(directory / "trial.json", row)
        return row
    result_path = directory / "adapter-result.json"
    replacements = {
        "request": str(directory / "request.json"),
        "workspace": str(workspace),
        "result": str(result_path),
        "python": sys.executable,
        "benchmark": str(HERE),
    }
    command = list(args.command)
    for key, value in replacements.items():
        command = [part.replace("{" + key + "}", value) for part in command]
    try:
        with tempfile.TemporaryDirectory(prefix="eve-benchmark-private-") as private:
            env = dict(os.environ, TMPDIR=private, TEMP=private, TMP=private)
            row["agent"] = run_process(
                command, workspace, task["timeoutSeconds"], directory / "agent.log", env=env
            )
        adapter_result = read_json(result_path) if result_path.exists() else None
        if adapter_result is not None:
            row["usage"] = usage_metrics(adapter_result)
        if (
            adapter_result is not None
            and adapter_result.get("failureKind") == "provider_policy_denied"
        ):
            row["status"] = "blocked"
            row["reason"] = "adapter observed an explicit HTTP CONNECT 403 from provider proxy"
        elif row["agent"]["timedOut"]:
            row["status"] = "timeout"
        elif row["agent"]["exitCode"] != 0:
            row["status"] = "adapter_error"
        else:
            after = fingerprint(workspace)
            changed = changed_files(before, after)
            row["changedFiles"] = changed
            write_json(directory / "final-files.json", after)
            if not set(changed) <= set(task["allowedFiles"]) or any(
                not (workspace / f).is_file() for f in FIXTURE_FILES
            ):
                row["status"] = "scope_violation"
            else:
                # Agent is stopped before a separate trusted process sees a frozen copy.
                project = directory / "evaluation-project"
                shutil.copytree(workspace, project)
                original = read_json(REPO / suite["fixture"] / "game.agent.json")
                grade_request = {
                    "project": str(project),
                    "category": task["category"],
                    "grader": task["grader"],
                    "seed": seed,
                    "engine": str(args.engine) if args.engine else None,
                    "originalContract": original,
                    "artifacts": str(directory),
                }
                write_json(directory / "grader-request.json", grade_request)
                grading = run_process(
                    [
                        sys.executable,
                        str(HERE / "grader_cli.py"),
                        str(directory / "grader-request.json"),
                        str(directory / "grade.json"),
                    ],
                    HERE,
                    args.grader_timeout,
                    directory / "grader.log",
                )
                row["grading"] = grading
                if grading["timedOut"] or grading["exitCode"] != 0:
                    row["status"] = "grader_error"
                else:
                    grade = read_json(directory / "grade.json")
                    if grade.get("schema") != "eve.agent-benchmark-grade/v1" or not grade.get(
                        "checks"
                    ):
                        raise ValueError("invalid grader result")
                    row["checks"] = grade["checks"]
                    row["status"] = (
                        "pass" if all(c["passed"] is True for c in row["checks"]) else "fail"
                    )
    except (OSError, ValueError, KeyError) as exc:
        row["status"] = "adapter_error"
        row["reason"] = str(exc)
    try:
        final = fingerprint(workspace)
        write_json(directory / "final-files.json", final)
        row.setdefault("changedFiles", changed_files(before, final))
    except ValueError as exc:
        row["status"] = "scope_violation"
        row["reason"] = str(exc)
    write_json(directory / "trial.json", row)
    return row


def run(args):
    suite = load_suite(args.suite)
    if args.engine:
        args.engine = args.engine.resolve(strict=True)
    config = read_json(args.adapter)
    if config.get("schema") != "eve.agent-benchmark-adapter/v1":
        raise ValueError("unsupported adapter schema")
    if config.get("calibrationOnly") and not args.calibration:
        raise ValueError("control adapters require --calibration")
    args.command = config["command"]
    if not args.command or not all(isinstance(p, str) for p in args.command):
        raise ValueError("adapter command must be a nonempty argv array")
    if not math.isfinite(args.grader_timeout) or args.grader_timeout <= 0:
        raise ValueError("grader timeout must be positive")
    if args.repeats < 1:
        raise ValueError("repeats must be positive")
    tasks = [t for t in suite["tasks"] if not args.task or t["id"] in args.task]
    if not tasks or (args.task and set(args.task) != {t["id"] for t in tasks}):
        raise ValueError("unknown/empty task selection")
    seeds = args.seeds if args.seeds is not None else suite["seeds"]
    if not seeds or len(set(seeds)) != len(seeds):
        raise ValueError("seeds must be nonempty and unique")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    ident = identity(suite, args)
    experiment = {
        "schema": "eve.agent-benchmark-experiment/v1",
        "createdUtc": datetime.now(timezone.utc).isoformat(),
        "adapter": config,
        "adapterSources": {p.name: digest(p) for p in sorted((HERE / "adapters").glob("*.py"))},
        "identity": ident,
        "orderSeed": args.order_seed,
        "tasks": [t["id"] for t in tasks],
        "seeds": seeds,
        "repeats": args.repeats,
        "calibration": args.calibration,
        "repositoryRevision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True, encoding="utf-8"
        ).strip(),
    }
    write_json(args.output / "experiment.json", experiment)
    schedule = [
        (task, seed, repeat) for task in tasks for seed in seeds for repeat in range(args.repeats)
    ]
    random.Random(args.order_seed).shuffle(schedule)
    rows = []
    for task, seed, repeat in schedule:
        directory = args.output / f"{task['id']}-s{seed}-r{repeat}"
        row = trial_run(suite, task, seed, repeat, args, directory)
        rows.append(row)
        print(f"{task['id']} seed={seed} repeat={repeat}: {row['status']}", flush=True)
        report = {
            "schema": "eve.agent-benchmark-report/v1",
            "experiment": experiment,
            "trials": rows,
            "complete": len(rows) == len(schedule),
            "summary": summarize(rows),
        }
        write_json(args.output / "report.json", report)
    return 0 if all(r["status"] == "pass" for r in rows) else 1


def validate_controls(args, report):
    expected = {(r["taskId"], r["seed"]) for r in report["trials"]}
    for path, status, adapter_id in (
        (args.positive_control, "pass", "reference"),
        (args.negative_control, "fail", "noop"),
    ):
        control = read_json(path)
        if (
            control.get("schema") != "eve.agent-benchmark-report/v1"
            or not control.get("complete")
            or not control["experiment"]["calibration"]
            or control["experiment"]["adapter"]["id"] != adapter_id
            or control["experiment"]["identity"] != report["experiment"]["identity"]
        ):
            raise ValueError("control provenance differs from experiment")
        covered = {(r["taskId"], r["seed"]) for r in control["trials"] if r["status"] == status}
        if not expected <= covered or any(r["status"] != status for r in control["trials"]):
            raise ValueError("controls must pass/fail as expected for every task and case seed")


def compare(args):
    a, b = read_json(args.baseline), read_json(args.candidate)
    for report in (a, b):
        if report.get("schema") != "eve.agent-benchmark-report/v1" or not report.get("complete"):
            raise ValueError("comparison requires complete versioned reports")
        if report["experiment"]["calibration"] or not report["summary"]["rankable"]:
            raise ValueError("calibration or infrastructure failures cannot be ranked")
    for key in ("identity", "tasks", "seeds", "repeats"):
        if a["experiment"][key] != b["experiment"][key]:
            raise ValueError(f"incomparable experiments: {key} differs")
    for report in (a, b):
        adapter = report["experiment"]["adapter"]
        if not adapter.get("model") or "unresolved" in adapter["model"] or not adapter.get("tools"):
            raise ValueError("comparison requires explicitly pinned model and declared tools")
    validate_controls(args, a)

    def indexed(report):
        return {(r["taskId"], r["seed"], r["repeat"]): r for r in report["trials"]}

    left, right = indexed(a), indexed(b)
    if (
        left.keys() != right.keys()
        or len(left) != len(a["trials"])
        or len(right) != len(b["trials"])
    ):
        raise ValueError("unmatched or duplicate trials")
    deltas = [
        int(right[k]["status"] == "pass") - int(left[k]["status"] == "pass") for k in sorted(left)
    ]
    rng = random.Random(0)
    boots = [sum(rng.choices(deltas, k=len(deltas))) / len(deltas) for _ in range(2000)]
    result = {
        "schema": "eve.agent-benchmark-comparison/v1",
        "pairedTrials": len(deltas),
        "successRateDelta": sum(deltas) / len(deltas),
        "pairedBootstrap95": [quantile(boots, 0.025), quantile(boots, 0.975)],
        "candidateWins": sum(d == 1 for d in deltas),
        "candidateLosses": sum(d == -1 for d in deltas),
        "baseline": a["summary"],
        "candidate": b["summary"],
        "uncertainty": "Conditional on these public tasks; repeated seeds are not independent task samples. Not population-level significance.",
    }
    write_json(args.output, result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    run_parser = commands.add_parser("run")
    run_parser.add_argument("--suite", type=Path, default=HERE / "suite.json")
    run_parser.add_argument("--adapter", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    run_parser.add_argument("--engine", type=Path)
    run_parser.add_argument("--environment-label", default="local-unspecified")
    run_parser.add_argument("--task", action="append")
    run_parser.add_argument("--seeds", type=int, nargs="+")
    run_parser.add_argument("--repeats", type=int, default=3)
    run_parser.add_argument("--order-seed", type=int, default=0)
    run_parser.add_argument("--grader-timeout", type=float, default=180)
    run_parser.add_argument("--calibration", action="store_true")
    compare_parser = commands.add_parser("compare")
    compare_parser.add_argument("baseline", type=Path)
    compare_parser.add_argument("candidate", type=Path)
    compare_parser.add_argument("--output", type=Path, required=True)
    compare_parser.add_argument("--positive-control", type=Path, required=True)
    compare_parser.add_argument("--negative-control", type=Path, required=True)
    args = parser.parse_args()
    try:
        return run(args) if args.operation == "run" else compare(args)
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(2, f"benchmark configuration error: {exc}\n")


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    raise SystemExit(main())
