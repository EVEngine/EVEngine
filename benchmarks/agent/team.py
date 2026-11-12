#!/usr/bin/env python3
"""Multi-agent campaign development: plan, isolated Git workspaces, merge, verify."""

from __future__ import annotations

import argparse
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import random
import shutil
import subprocess
import sys
import tempfile
import time

from common import digest, read_json, run_process, usage_metrics, write_json
from team_project import (
    FILES,
    SCENARIOS,
    create_project,
    integration_checks,
    requirement_checks,
    spec_for,
    validate_plan,
)

HERE = Path(__file__).resolve().parent


def tree(root):
    result = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if relative.parts[0] == ".git":
            if relative.as_posix() == ".git" and (path.is_symlink() or not path.is_dir()):
                raise ValueError("workspace Git directory replaced")
            continue
        if path.is_symlink():
            raise ValueError(f"symlink forbidden: {relative}")
        if path.is_file():
            result[relative.as_posix()] = digest(path)
    return result


def changed(before, after):
    return sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))


def git(root, *arguments, check=True):
    # Keep benchmark commits local and independent of user identity, hooks and
    # inherited Git directory/index variables. Never push or edit the source repo.
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
    result = subprocess.run(
        [
            "git",
            "-c",
            "user.name=Agent Benchmark",
            "-c",
            "user.email=benchmark@localhost",
            "-c",
            f"core.hooksPath={os.devnull}",
            "-c",
            "commit.gpgSign=false",
            "-C",
            str(root),
            *arguments,
        ],
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=15,
    )
    if check and result.returncode:
        raise ValueError(f"git {' '.join(arguments[:2])}: {result.stderr}")
    return result


def initialize(root):
    git(root, "init", "-q")
    git(root, "add", ".")
    git(root, "commit", "-qm", "Frozen campaign baseline")


def clone(source, target):
    git(source, "clone", "--no-hardlinks", "--quiet", str(source), str(target))


def execute(adapter, request, directory, timeout, origin):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "artifacts").mkdir()
    request = dict(
        request,
        schema="eve.agent-benchmark-request/v1",
        artifacts=str(directory / "artifacts"),
        timeoutSeconds=timeout,
    )
    write_json(directory / "request.json", request)
    result_path = directory / "result.json"
    replacements = {
        "request": str(directory / "request.json"),
        "result": str(result_path),
        "workspace": request["workspace"],
        "benchmark": str(HERE),
        "python": sys.executable,
    }
    command = list(adapter["command"])
    for key, value in replacements.items():
        command = [part.replace("{" + key + "}", value) for part in command]
    started = time.monotonic() - origin
    result = {
        "role": request["role"],
        "taskId": request["taskId"],
        "startedSeconds": started,
        "usage": {},
    }
    try:
        with tempfile.TemporaryDirectory(prefix="eve-team-private-") as private:
            env = dict(os.environ, TMPDIR=private, TEMP=private, TMP=private)
            outcome = run_process(
                command, request["workspace"], timeout, directory / "agent.log", env=env
            )
        result.update(outcome)
        value = read_json(result_path) if result_path.exists() else None
        if value is not None:
            result["usage"] = usage_metrics(value)
        if value and value.get("failureKind") == "provider_policy_denied":
            result["status"] = "blocked"
        elif outcome["timedOut"]:
            result["status"] = "timeout"
        elif outcome["exitCode"] != 0:
            result["status"] = "adapter_error"
        else:
            result["status"] = "completed"
    except (OSError, ValueError, KeyError, TypeError) as exc:
        result.update(
            status="adapter_error", error=str(exc), wallSeconds=time.monotonic() - origin - started
        )
    result["finishedSeconds"] = time.monotonic() - origin
    write_json(directory / "execution.json", result)
    return result


def request_for(role, workspace, spec, identifier, allowed, **extras):
    return {
        "role": role,
        "workspace": str(workspace),
        "taskId": identifier,
        "seed": spec["seed"],
        "projectSpec": spec,
        "allowedFiles": allowed,
        "engine": None,
        "rules": "No human assistance. Only edit allowedFiles. Do not edit .bench-owner.json or other workspaces. Do not change evaluation infrastructure. Git operations must stay in your own repository.",
        **extras,
    }


def stage_prompt(role, spec, task=None):
    if role == "planner":
        return (
            spec["brief"]
            + "\nWrite plan.json with schema=eve.agent-team-plan/v1 and tasks=[{id,requirements:[requirement ids],allowedFiles:[exact paths],dependsOn:[task ids]}]. "
            "Use 2..8 productive tasks; assign each requirement exactly once. Declare all necessary cross-task dependencies (transitive paths allowed). "
            "Workers start from the SAME baseline; completed upstream changes are provided as dependencyOutputs, not automatically installed. "
            "Consider exclusive ownership for shared files and provide any coordination choices in each task's optional instructions. "
            "Only plan.json may be edited. You may not implement the project."
        )
    if role == "worker":
        return f"{spec['brief']}\nImplement ONLY task {json.dumps(task, ensure_ascii=False)}. Read projectSpec and dependencyOutputs in the request. Write project JSON files, not a verbal completion. Other workers have independent repositories."
    return (
        spec["brief"]
        + "\nIntegrate worker changes. Resolve any real Git conflict markers and preserve all worker requirements. "
        "Also fix semantic integration errors even if Git merged cleanly. Only edit the listed project files. "
        "You may git add/commit your resolution; the harness will also stage a valid uncommitted resolution. Do not delete another worker's changes to make Git succeed."
    )


def freeze_worker(base, workspace, task, trusted, directory):
    before, after = tree(base), tree(workspace)
    touched = changed(before, {k: v for k, v in after.items() if k != ".bench-owner.json"})
    if not set(touched) <= set(task["allowedFiles"]):
        return None, touched
    clone(base, trusted)
    for name in touched:
        target = trusted / name
        if (workspace / name).exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(workspace / name, target)
        else:
            target.unlink()
    git(trusted, "add", "-A")
    git(trusted, "commit", "--allow-empty", "-qm", f"Candidate {task['id']}")
    sha = git(trusted, "rev-parse", "HEAD").stdout.strip()
    patch = git(trusted, "show", "--format=fuller", "--stat", "--patch", "HEAD").stdout
    (directory / "candidate.patch").write_text(patch, encoding="utf-8")
    return sha, touched


def provision_workspaces(tasks, base, root, spec, adapter, args, origin):
    workers_root = root / "workers"
    workers_root.mkdir()
    before = tree(base)
    before_head = git(base, "rev-parse", "HEAD").stdout.strip()
    execution = None
    if args.workspace_mode == "agent":
        request = request_for(
            "workspace-manager",
            workers_root,
            spec,
            "workspace-setup",
            [f"{task['id']}/workspace/" for task in tasks],
            baselinePath=str(base),
            workspaceTargets=[task["id"] for task in tasks],
            prompt="Create one independent Git repository per task at <task-id>/workspace inside your current directory, initialized exactly from baselinePath. Use independent Git metadata and copied working files, not shared worktrees, symlinks or hardlinks. Do not modify the baseline. Do not implement any requirement. Targets and baselinePath are in the request. Only the listed directories may be created.",
        )
        execution = execute(
            adapter, request, root / "workspace-setup", args.workspace_timeout, origin
        )
    else:
        for task in tasks:
            destination = workers_root / task["id"] / "workspace"
            destination.parent.mkdir()
            clone(base, destination)
    errors, repositories, inodes = [], set(), set()
    if execution and execution["status"] != "completed":
        errors.append("workspace manager did not complete")
    baseline_unchanged = (
        tree(base) == before
        and git(base, "rev-parse", "HEAD").stdout.strip() == before_head
        and not git(base, "status", "--porcelain").stdout.strip()
    )
    if not baseline_unchanged:
        errors.append("workspace manager modified baseline")
    if {p.name for p in workers_root.iterdir()} != {t["id"] for t in tasks}:
        errors.append("unexpected/missing workspace directories")
    for task in tasks:
        workspace = workers_root / task["id"] / "workspace"
        try:
            if (
                workspace.is_symlink()
                or workspace.parent.is_symlink()
                or {p.name for p in workspace.parent.iterdir()} != {"workspace"}
                or tree(workspace) != before
            ):
                raise ValueError("workspace does not match frozen baseline")
            common_dir = Path(
                git(
                    workspace, "rev-parse", "--path-format=absolute", "--git-common-dir"
                ).stdout.strip()
            ).resolve()
            if (
                common_dir != (workspace / ".git").resolve()
                or common_dir in repositories
                or not (workspace / ".git").is_dir()
            ):
                raise ValueError("shared or invalid Git metadata")
            repositories.add(common_dir)
            for name in FILES:
                info = (workspace / name).stat()
                identity = (info.st_dev, info.st_ino)
                if info.st_nlink != 1 or identity in inodes:
                    raise ValueError("hardlinked/shared working file")
                inodes.add(identity)
        except (OSError, ValueError) as exc:
            errors.append(f"{task['id']}: {exc}")
    result = {
        "mode": args.workspace_mode,
        "execution": execution,
        "passed": not errors,
        "errors": errors,
        "independentRepositories": len(repositories),
        "baselineUnchanged": baseline_unchanged,
    }
    write_json(root / "workspace-provisioning.json", result)
    return result


def run_workers(tasks, base, root, spec, adapter, args, origin):
    workspaces = {}
    guards = {}
    for task in tasks:
        workspace = root / "workers" / task["id"] / "workspace"
        write_json(workspace / ".bench-owner.json", {"owner": task["id"], "trial": str(root)})
        workspaces[task["id"]] = workspace
        guards[task["id"]] = tree(workspace)[".bench-owner.json"]
    baseline_hashes = tree(base)
    baseline_head = git(base, "rev-parse", "HEAD").stdout.strip()
    results = {}
    pending = {t["id"]: t for t in tasks}
    phase_start = time.monotonic()
    slots = args.workers if args.schedule == "parallel" else 1
    with ThreadPoolExecutor(max_workers=slots) as pool:
        futures = {}
        while pending or futures:
            for task in tasks:
                if task["id"] not in pending or not all(d in results for d in task["dependsOn"]):
                    continue
                if any(results[d]["status"] != "completed" for d in task["dependsOn"]):
                    results[task["id"]] = {
                        "status": "dependency_failed",
                        "taskId": task["id"],
                        "role": "worker",
                        "usage": {},
                        "wallSeconds": 0,
                    }
                    del pending[task["id"]]
                elif len(futures) < slots:
                    dependency_outputs = {
                        d: results[d].get("outputs", {}) for d in task["dependsOn"]
                    }
                    request = request_for(
                        "worker",
                        workspaces[task["id"]],
                        spec,
                        task["id"],
                        task["allowedFiles"],
                        task=task,
                        dependencyOutputs=dependency_outputs,
                        prompt=stage_prompt("worker", spec, task),
                    )
                    future = pool.submit(
                        execute,
                        adapter,
                        request,
                        root / "workers" / task["id"] / "run",
                        args.worker_timeout,
                        origin,
                    )
                    futures[future] = task
                    del pending[task["id"]]
            if not futures:
                if pending:
                    continue
                break
            completed, _ = wait(futures, return_when=FIRST_COMPLETED)
            for future in completed:
                task = futures.pop(future)
                result = future.result()
                workspace = workspaces[task["id"]]
                try:
                    if tree(workspace).get(".bench-owner.json") != guards[task["id"]]:
                        result["status"] = "scope_violation"
                    if result["status"] == "completed":
                        trusted = root / "candidates" / task["id"]
                        trusted.parent.mkdir(exist_ok=True)
                        sha, touched = freeze_worker(
                            base, workspace, task, trusted, root / "workers" / task["id"]
                        )
                        result.update(commit=sha, changedFiles=touched)
                        if sha is None:
                            result["status"] = "scope_violation"
                        else:
                            result["checks"] = requirement_checks(
                                trusted, spec, task["requirements"]
                            )
                            result["outputs"] = {
                                name: (trusted / name).read_text(encoding="utf-8")
                                for name in touched
                                if (trusted / name).is_file()
                            }
                            if not all(c["passed"] for c in result["checks"]):
                                result["status"] = "local_failure"
                except (ValueError, OSError) as exc:
                    result.update(status="scope_violation", error=str(exc))
                results[task["id"]] = result
                write_json(root / "workers" / task["id"] / "worker.json", result)
                print(f"  worker {task['id']}: {result['status']}", flush=True)
    violations = []
    baseline_unchanged = (
        tree(base) == baseline_hashes
        and git(base, "rev-parse", "HEAD").stdout.strip() == baseline_head
        and not git(base, "status", "--porcelain").stdout.strip()
    )
    if not baseline_unchanged:
        violations.append("frozen baseline modified")
    for task in tasks:
        try:
            final = tree(workspaces[task["id"]])
            actual = changed(
                baseline_hashes, {k: v for k, v in final.items() if k != ".bench-owner.json"}
            )
            if final.get(".bench-owner.json") != guards[task["id"]] or not set(actual) <= set(
                task["allowedFiles"]
            ):
                violations.append(task["id"])
            # Candidate was frozen when the worker exited: detect later sibling writes.
            if results[task["id"]].get("commit") and final != {
                **tree(root / "candidates" / task["id"]),
                ".bench-owner.json": guards[task["id"]],
            }:
                violations.append(f"{task['id']}: changed after worker exit")
        except (OSError, ValueError) as exc:
            violations.append(str(exc))
    intervals = [(r["startedSeconds"], 1) for r in results.values() if "startedSeconds" in r] + [
        (r["finishedSeconds"], -1) for r in results.values() if "finishedSeconds" in r
    ]
    active = peak = 0
    for _, delta in sorted(intervals):
        active += delta
        peak = max(active, peak)
    return results, {
        "baselineUnchanged": baseline_unchanged,
        "violations": violations,
        "workspaceCount": len(workspaces),
        "independentGitDirectories": len({(w / ".git").resolve() for w in workspaces.values()})
        == len(workspaces),
        "peakConcurrentAdapters": peak,
        "workerPhaseWallSeconds": time.monotonic() - phase_start,
    }


def integrate(base, root, spec, tasks, workers, adapter, args, origin):
    workspace = root / "integration" / "workspace"
    workspace.parent.mkdir()
    clone(base, workspace)
    events, executions = [], []
    for task in tasks:
        result = workers[task["id"]]
        if not result.get("commit"):
            continue
        candidate = root / "candidates" / task["id"]
        git(workspace, "fetch", "--quiet", str(candidate), result["commit"])
        merge = git(workspace, "merge", "--no-ff", "--no-edit", result["commit"], check=False)
        conflicts = git(workspace, "diff", "--name-only", "--diff-filter=U").stdout.splitlines()
        event = {
            "taskId": task["id"],
            "commit": result["commit"],
            "exitCode": merge.returncode,
            "conflictFiles": conflicts,
        }
        events.append(event)
        if merge.returncode and not conflicts:
            return {
                "status": "merge_error",
                "events": events,
                "executions": executions,
                "reason": merge.stderr,
            }
        if conflicts:
            execution, accepted = integrate_call(
                workspace, root, spec, adapter, args, origin, f"conflict-{task['id']}"
            )
            executions.append(execution)
            event["resolutionAccepted"] = accepted
            if not accepted:
                return {"status": "merge_failed", "events": events, "executions": executions}
    automatic_checks = integration_checks(workspace, spec)
    execution, accepted = integrate_call(
        workspace, root, spec, adapter, args, origin, "final-integration"
    )
    executions.append(execution)
    checks = integration_checks(workspace, spec) if accepted else []
    return {
        "status": "pass"
        if accepted and checks and all(c["passed"] for c in checks)
        else "integration_failed",
        "events": events,
        "executions": executions,
        "checks": checks,
        "finalCommit": git(workspace, "rev-parse", "HEAD").stdout.strip(),
        "beforeFinalIntegrationChecks": automatic_checks,
        "semanticRepairRequired": not all(c["passed"] for c in automatic_checks),
    }


def integrate_call(workspace, root, spec, adapter, args, origin, identifier):
    before = tree(workspace)
    before_commit = git(workspace, "rev-parse", "HEAD").stdout.strip()
    request = request_for(
        "integrator",
        workspace,
        spec,
        identifier,
        list(FILES),
        prompt=stage_prompt("integrator", spec),
    )
    result = execute(
        adapter, request, root / "integration" / identifier, args.integrator_timeout, origin
    )
    try:
        after = tree(workspace)
    except (OSError, ValueError) as exc:
        result.update(status="scope_violation", error=str(exc), resolutionAccepted=False)
        write_json(root / "integration" / identifier / "integration.json", result)
        return result, False
    result["changedFiles"] = changed(before, after)
    accepted = result["status"] == "completed" and set(result["changedFiles"]) <= set(FILES)
    result["diffNumstat"] = git(workspace, "diff", "--numstat", "HEAD").stdout
    if accepted:
        # Syntax must be valid before staging; unresolved markers are not a merge.
        try:
            for name in FILES:
                read_json(workspace / name)
            git(workspace, "add", "--", *FILES)
            if git(workspace, "diff", "--name-only", "--diff-filter=U").stdout.strip():
                accepted = False
            elif (workspace / ".git/MERGE_HEAD").exists() or git(
                workspace, "status", "--porcelain"
            ).stdout:
                git(workspace, "commit", "--allow-empty", "-qm", f"Integration {identifier}")
        except (ValueError, OSError):
            accepted = False
    result["resolutionAccepted"] = accepted
    result["finalDiffNumstat"] = (
        git(workspace, "diff", "--numstat", before_commit).stdout
        if accepted
        else result.get("diffNumstat", "")
    )
    write_json(root / "integration" / identifier / "integration.json", result)
    return result, accepted


def run_trial(root, scenario, seed, args, adapters):
    root.mkdir()
    spec = spec_for(scenario, seed)
    origin = time.monotonic()
    base = root / "baseline"
    create_project(base, spec)
    initialize(base)
    initial = tree(base)
    initial_head = git(base, "rev-parse", "HEAD").stdout.strip()
    write_json(root / "baseline-files.json", initial)
    planner = root / "planner" / "workspace"
    planner.parent.mkdir()
    clone(base, planner)
    request = request_for(
        "planner", planner, spec, "decompose", ["plan.json"], prompt=stage_prompt("planner", spec)
    )
    planning = execute(
        adapters["planner"], request, root / "planner" / "run", args.planner_timeout, origin
    )
    row = {
        "scenario": scenario,
        "seed": seed,
        "status": "planning_failed",
        "planning": planning,
        "workers": {},
        "integration": {},
    }
    try:
        if planning["status"] != "completed":
            return row
        if (
            not set(changed(initial, tree(planner))) <= {"plan.json"}
            or tree(base) != initial
            or git(base, "rev-parse", "HEAD").stdout.strip() != initial_head
        ):
            row["status"] = "scope_violation"
            return row
        try:
            plan = read_json(planner / "plan.json")
        except (OSError, ValueError) as exc:
            row.update(status="invalid_plan", decomposition={"valid": False, "errors": [str(exc)]})
            return row
        decomposition = validate_plan(plan, spec)
        row["decomposition"] = decomposition
        write_json(root / "plan.json", plan)
        row["planSha256"] = digest(root / "plan.json")
        if not decomposition["valid"]:
            row["status"] = "invalid_plan"
            return row
        provisioning = provision_workspaces(
            plan["tasks"], base, root, spec, adapters["workspace"], args, origin
        )
        row["workspaceProvisioning"] = provisioning
        if not provisioning["passed"]:
            row["status"] = "workspace_failed"
            return row
        workers, isolation = run_workers(
            plan["tasks"], base, root, spec, adapters["worker"], args, origin
        )
        row.update(workers=workers, isolation=isolation)
        if isolation["violations"]:
            row["status"] = "scope_violation"
            return row
        row["integration"] = integrate(
            base, root, spec, plan["tasks"], workers, adapters["integrator"], args, origin
        )
        local_pass = all(r["status"] == "completed" for r in workers.values())
        row["status"] = "pass" if local_pass and row["integration"]["status"] == "pass" else "fail"
        return row
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        row.update(status="evaluation_error", error=str(exc))
        return row
    finally:
        row["wallSeconds"] = time.monotonic() - origin
        executions = [planning, *row["workers"].values(), *row["integration"].get("executions", [])]
        if row.get("workspaceProvisioning", {}).get("execution"):
            executions.append(row["workspaceProvisioning"]["execution"])
        row["agentSecondsSum"] = sum(r.get("wallSeconds", 0) for r in executions)
        row["conflictCount"] = sum(
            bool(e["conflictFiles"]) for e in row["integration"].get("events", [])
        )
        row["integrationAgentSeconds"] = sum(
            r["wallSeconds"] for r in row["integration"].get("executions", [])
        )
        row["integrationEditLines"] = sum(
            sum(int(n) for n in line.split()[:2] if n.isdigit())
            for r in row["integration"].get("executions", [])
            for line in r.get("finalDiffNumstat", r.get("diffNumstat", "")).splitlines()
        )
        local_checks = [
            c for worker in row["workers"].values() for c in worker.get("checks", []) if c["passed"]
        ]
        final_checks = {c["id"]: c["passed"] for c in row["integration"].get("checks", [])}
        row["workerRequirementRetention"] = (
            sum(final_checks.get(c["id"], False) for c in local_checks) / len(local_checks)
            if local_checks and final_checks
            else None
        )
        row["usage"] = {}
        for key in ("inputTokens", "outputTokens", "costUsd", "toolCalls", "humanInterventions"):
            values = [r.get("usage", {}).get(key) for r in executions]
            row["usage"][key] = sum(values) if all(v is not None for v in values) else None
        row["infrastructureError"] = row["status"] == "evaluation_error" or any(
            r["status"] in ("blocked", "adapter_error") for r in executions
        )
        write_json(root / "trial.json", row)


def summarize_team(rows, calibration=False):
    scenarios = {}
    for scenario in sorted({r["scenario"] for r in rows}):
        cases = [r for r in rows if r["scenario"] == scenario]

        def mean(values):
            values = [v for v in values if v is not None]
            return sum(values) / len(values) if values else None

        conflicts = [
            e for r in cases for e in r["integration"].get("events", []) if e["conflictFiles"]
        ]
        scenarios[scenario] = {
            "trials": len(cases),
            "passed": sum(r["status"] == "pass" for r in cases),
            "validPlanRate": mean(
                [int(r.get("decomposition", {}).get("valid", False)) for r in cases]
            ),
            "workspaceProvisionSuccessRate": mean(
                [
                    int(r["workspaceProvisioning"]["passed"])
                    for r in cases
                    if "workspaceProvisioning" in r
                ]
            ),
            "isolationViolationTrials": sum(
                bool(r.get("isolation", {}).get("violations")) for r in cases
            ),
            "meanPlanSeconds": mean([r["planning"].get("wallSeconds") for r in cases]),
            "meanDeclaredWriteOverlapPairs": mean(
                [
                    len(r["decomposition"].get("writeOverlapPairs", []))
                    for r in cases
                    if "decomposition" in r
                ]
            ),
            "meanCriticalPathTaskCount": mean(
                [r.get("decomposition", {}).get("criticalPathTaskCount") for r in cases]
            ),
            "meanRunnableWidth": mean(
                [r.get("decomposition", {}).get("maxRunnableWidth") for r in cases]
            ),
            "observedPeakConcurrentAdapters": max(
                [r.get("isolation", {}).get("peakConcurrentAdapters", 0) for r in cases]
            ),
            "conflicts": len(conflicts),
            "resolvedConflicts": sum(e.get("resolutionAccepted", False) for e in conflicts),
            "meanIntegrationAgentSeconds": mean([r["integrationAgentSeconds"] for r in cases]),
            "meanIntegrationEditLines": mean([r["integrationEditLines"] for r in cases]),
            "meanWorkerRequirementRetention": mean(
                [r["workerRequirementRetention"] for r in cases]
            ),
            "meanProjectWallSeconds": mean([r["wallSeconds"] for r in cases]),
            "meanAgentSecondsSum": mean([r["agentSecondsSum"] for r in cases]),
        }
    return {
        "trials": len(rows),
        "passed": sum(r["status"] == "pass" for r in rows),
        "rankable": bool(rows)
        and not calibration
        and not any(r["infrastructureError"] for r in rows),
        "byScenario": scenarios,
    }


def load_adapter(path, calibration):
    config = read_json(path)
    if (
        config.get("schema") != "eve.agent-benchmark-adapter/v1"
        or not config.get("command")
        or not all(isinstance(p, str) for p in config["command"])
    ):
        raise ValueError("invalid adapter config")
    if config.get("calibrationOnly") and not calibration:
        raise ValueError("control adapters require --calibration")
    return config


def run(args):
    if (
        not 1 <= args.workers <= 8
        or args.repeats < 1
        or not args.seeds
        or len(set(args.seeds)) != len(args.seeds)
        or (args.scenario and len(set(args.scenario)) != len(args.scenario))
    ):
        raise ValueError("invalid worker/repeat/seed configuration")
    if any(
        not math.isfinite(v) or v <= 0
        for v in (
            args.planner_timeout,
            args.workspace_timeout,
            args.worker_timeout,
            args.integrator_timeout,
        )
    ):
        raise ValueError("timeouts must be finite and positive")
    adapters = {
        role: load_adapter(getattr(args, f"{role}_adapter") or args.adapter, args.calibration)
        for role in ("planner", "workspace", "worker", "integrator")
    }
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    scenarios = args.scenario or list(SCENARIOS)
    identity = {
        "schema": "eve.agent-team-identity/v1",
        "suite": "campaign-collaboration/v1",
        "sources": {
            p.name: digest(p)
            for p in (HERE / "team.py", HERE / "team_project.py", HERE / "common.py")
        },
        "environmentLabel": args.environment_label,
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cpuCount": os.cpu_count(),
        "workers": args.workers,
        "workspaceMode": args.workspace_mode,
        "budgets": [
            args.planner_timeout,
            args.workspace_timeout,
            args.worker_timeout,
            args.integrator_timeout,
        ],
        "track": "content-integration-not-engine-runtime",
        "gitVersion": subprocess.check_output(
            ["git", "--version"], text=True, encoding="utf-8"
        ).strip(),
    }
    experiment = {
        "identity": identity,
        "adapters": adapters,
        "schedule": args.schedule,
        "calibration": args.calibration,
        "createdUtc": datetime.now(timezone.utc).isoformat(),
        "orderSeed": args.order_seed,
        "scenarios": scenarios,
        "seeds": args.seeds,
        "repeats": args.repeats,
        "adapterSources": {p.name: digest(p) for p in sorted((HERE / "adapters").glob("*.py"))},
    }
    write_json(root / "experiment.json", experiment)
    schedule = [(s, seed, r) for s in scenarios for seed in args.seeds for r in range(args.repeats)]
    random.Random(args.order_seed).shuffle(schedule)
    rows = []
    for scenario, seed, repeat in schedule:
        print(f"{scenario} seed={seed} repeat={repeat}", flush=True)
        row = run_trial(root / f"{scenario}-s{seed}-r{repeat}", scenario, seed, args, adapters)
        row["repeat"] = repeat
        rows.append(row)
        summary = summarize_team(rows, args.calibration)
        write_json(
            root / "report.json",
            {
                "schema": "eve.agent-team-report/v1",
                "experiment": experiment,
                "complete": len(rows) == len(schedule),
                "trials": rows,
                "summary": summary,
            },
        )
        print(f"  project: {row['status']}", flush=True)
    return 0 if all(r["status"] == "pass" for r in rows) else 1


def compare(args):
    left, right = read_json(args.baseline), read_json(args.candidate)
    for report in (left, right):
        if (
            report.get("schema") != "eve.agent-team-report/v1"
            or not report.get("complete")
            or not report["summary"]["rankable"]
            or report["experiment"]["calibration"]
        ):
            raise ValueError(
                "only complete real experiments without infrastructure errors can be compared"
            )
        for adapter in report["experiment"]["adapters"].values():
            if (
                not adapter.get("model")
                or "unresolved" in adapter["model"]
                or not adapter.get("tools")
            ):
                raise ValueError("pin model and tools for every role")
    for key in ("identity", "scenarios", "seeds", "repeats"):
        if left["experiment"][key] != right["experiment"][key]:
            raise ValueError(f"incomparable team experiments: {key}")
    if args.axis == "tools" and left["experiment"]["schedule"] != right["experiment"]["schedule"]:
        raise ValueError("scheduler differs; use --axis scheduler for serial/parallel ablation")
    if (
        args.axis == "scheduler"
        and left["experiment"]["adapters"] != right["experiment"]["adapters"]
    ):
        raise ValueError("scheduler ablation requires identical role adapters")

    def index(report):
        return {(r["scenario"], r["seed"], r["repeat"]): r for r in report["trials"]}

    a, b = index(left), index(right)
    if (
        not a
        or a.keys() != b.keys()
        or len(a) != len(left["trials"])
        or len(b) != len(right["trials"])
    ):
        raise ValueError("missing or duplicate paired trials")
    for path, expected in ((args.positive_control, "pass"), (args.negative_control, "fail")):
        control = read_json(path)
        if (
            control.get("schema") != "eve.agent-team-report/v1"
            or not control.get("complete")
            or not control["experiment"]["calibration"]
            or control["experiment"]["identity"] != left["experiment"]["identity"]
            or any(r["infrastructureError"] for r in control["trials"])
        ):
            raise ValueError("invalid team control provenance")
        expected_id = "team-reference" if expected == "pass" else "team-noop"
        if any(
            adapter.get("id") != expected_id or not adapter.get("calibrationOnly")
            for adapter in control["experiment"]["adapters"].values()
        ):
            raise ValueError("team comparison requires reference/noop controls")
        coverage = {
            (r["scenario"], r["seed"]) for r in control["trials"] if r["status"] == expected
        }
        if not {(s, seed) for s, seed, _ in a} <= coverage or any(
            r["status"] != expected for r in control["trials"]
        ):
            raise ValueError("team controls did not pass/fail as expected")
    pairs = []
    for key in sorted(a):
        x, y = a[key], b[key]
        if args.axis == "scheduler" and x.get("planSha256") != y.get("planSha256"):
            raise ValueError("scheduler ablation requires identical decomposition plans")
        pairs.append(
            {
                "case": key,
                "baselineStatus": x["status"],
                "candidateStatus": y["status"],
                "successDelta": int(y["status"] == "pass") - int(x["status"] == "pass"),
                "wallSpeedup": x["wallSeconds"] / y["wallSeconds"]
                if x["status"] == y["status"] == "pass"
                else None,
                "agentSecondsDelta": y["agentSecondsSum"] - x["agentSecondsSum"],
                "conflictDelta": y["conflictCount"] - x["conflictCount"],
            }
        )
    write_json(
        args.output,
        {
            "schema": "eve.agent-team-comparison/v1",
            "axis": args.axis,
            "pairs": pairs,
            "warning": "Speedup only for paired successful projects. Tiny public content tasks do not establish native engine or general software productivity.",
        },
    )
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    p = sub.add_parser("run")
    p.add_argument("--adapter", type=Path, required=True)
    for role in ("planner", "workspace", "worker", "integrator"):
        p.add_argument(f"--{role}-adapter", type=Path)
        p.add_argument(f"--{role}-timeout", type=float, default=120 if role != "worker" else 240)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--scenario", choices=SCENARIOS, action="append")
    p.add_argument("--seeds", nargs="+", type=int, default=[17, 42, 83])
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument("--workers", type=int, default=3)
    p.add_argument("--schedule", choices=("parallel", "serial"), default="parallel")
    p.add_argument("--workspace-mode", choices=("agent", "harness"), default="agent")
    p.add_argument("--order-seed", type=int, default=0)
    p.add_argument("--environment-label", default="local-unspecified")
    p.add_argument("--calibration", action="store_true")
    p = sub.add_parser("compare")
    p.add_argument("baseline", type=Path)
    p.add_argument("candidate", type=Path)
    p.add_argument("--positive-control", type=Path, required=True)
    p.add_argument("--negative-control", type=Path, required=True)
    p.add_argument("--axis", choices=("tools", "scheduler"), default="tools")
    p.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        return run(args) if args.operation == "run" else compare(args)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(2, f"team benchmark configuration error: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
