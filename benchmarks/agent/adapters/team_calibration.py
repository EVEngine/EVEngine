#!/usr/bin/env python3
"""Cooperation controls. Never use these scripted solutions as model scores."""

import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import read_json, write_json  # noqa: E402
from team_project import initial_files, reference_edit, reference_integrate, reference_plan  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=(
            "reference",
            "no-merge",
            "bad-plan",
            "leak",
            "drop-change",
            "shared-workspace",
            "noop",
        ),
    )
    parser.add_argument("request", type=Path)
    parser.add_argument("result", type=Path)
    args = parser.parse_args()
    request = read_json(args.request)
    workspace = Path(request["workspace"])
    spec = request["projectSpec"]
    if request["role"] == "planner":
        plan = reference_plan(spec)
        if args.mode == "bad-plan":
            plan["tasks"][0]["requirements"] = []
        write_json(workspace / "plan.json", plan)
    elif request["role"] == "workspace-manager":
        from team import clone
        import os

        for identifier in request["workspaceTargets"]:
            destination = workspace / identifier / "workspace"
            destination.parent.mkdir()
            clone(Path(request["baselinePath"]), destination)
            if args.mode == "shared-workspace":
                target = destination / "catalog.json"
                target.unlink()
                os.link(Path(request["baselinePath"]) / "catalog.json", target)
    elif request["role"] == "worker":
        # Enough overlap to test the scheduler, not a model performance number.
        time.sleep(0.15)
        if args.mode != "noop":
            reference_edit(workspace, spec, request["task"]["requirements"])
        if args.mode == "no-merge" and request["taskId"] == "world":
            write_json(workspace / "world.json", initial_files(spec)["world.json"])
        if args.mode == "leak" and request["taskId"] == "village":
            sibling = workspace.parents[1] / "forest/workspace/.bench-owner.json"
            sibling.write_text('{"owner":"contaminated"}', encoding="utf-8")
    elif args.mode not in ("no-merge", "noop"):
        reference_integrate(workspace, spec)
        if args.mode == "drop-change":
            catalog = read_json(workspace / "catalog.json")
            del catalog["assets"]["forest.landmark"]
            write_json(workspace / "catalog.json", catalog)
    write_json(args.result, {"schema": "eve.agent-benchmark-adapter-result/v1", "usage": {}})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
