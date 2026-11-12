"""Fixed campaign authoring tasks and independent content integration checks.

This is a JSON content/coordination track, not a native engine playtest.
"""

from __future__ import annotations

from collections import Counter

from common import read_json, write_json

LEVELS = ("village", "forest", "mine")
SCENARIOS = ("independent-levels", "shared-catalog", "dependency-chain")
FILES = ("world.json", "catalog.json", *(f"levels/{name}.json" for name in LEVELS))


def spec_for(scenario, seed):
    if scenario not in SCENARIOS:
        raise ValueError("unknown team scenario")
    requirements = []
    for index, name in enumerate(LEVELS):
        requirements.append(
            {
                "id": f"level-{name}",
                "files": [f"levels/{name}.json"],
                "description": f"Author ready level {name}, prefab {name}.landmark, budget {100 + seed % 13 + index}, correct campaign ports.",
                "dependsOn": [f"level-{LEVELS[index - 1]}"]
                if scenario == "dependency-chain" and index
                else [],
            }
        )
    if scenario == "shared-catalog":
        for name in LEVELS:
            requirements.append(
                {
                    "id": f"asset-{name}",
                    "files": ["catalog.json"],
                    "description": f"Register {name}.landmark with mesh {name}.glb; preserve other registrations.",
                    "dependsOn": [],
                }
            )
    if scenario == "dependency-chain":
        requirements.append(
            {
                "id": "world-links",
                "files": ["world.json"],
                "description": "Connect village -> forest -> mine using the actual exported exit/entry ports; keep spawn village and destination mine.",
                "dependsOn": [f"level-{name}" for name in LEVELS],
            }
        )
    return {
        "schema": "eve.agent-team-project/v1",
        "scenario": scenario,
        "seed": seed,
        "levels": list(LEVELS),
        "requirements": requirements,
        "portRule": "entry/exit"
        if scenario != "dependency-chain"
        else "<level>_in_<seed> / <level>_out_<seed>",
        "brief": "Deliver a coherent three-level campaign: village -> forest -> mine. Every level ready, all landmark prefabs registered, all connections valid. Keep schemas and budgets. Plan ownership and dependencies before parallel development.",
    }


def target_level(spec, name):
    index = LEVELS.index(name)
    dynamic = spec["scenario"] == "dependency-chain"
    return {
        "schema": "eve.benchmark.level/v1",
        "id": name,
        "ready": True,
        "entry": f"{name}_in_{spec['seed']}" if dynamic else "entry",
        "exit": f"{name}_out_{spec['seed']}" if dynamic else "exit",
        "prefab": f"{name}.landmark",
        "budget": 100 + spec["seed"] % 13 + index,
    }


def target_world(spec):
    return {
        "schema": "eve.benchmark.world/v1",
        "spawn": "village",
        "destination": "mine",
        "connections": [
            {
                "from": a,
                "exit": target_level(spec, a)["exit"],
                "to": b,
                "entry": target_level(spec, b)["entry"],
            }
            for a, b in zip(LEVELS, LEVELS[1:])
        ],
    }


def initial_files(spec):
    static_spec = dict(spec, scenario="independent-levels")
    data = {
        "world.json": target_world(static_spec),
        "catalog.json": {"schema": "eve.benchmark.catalog/v1", "assets": {}},
    }
    if spec["scenario"] != "shared-catalog":
        data["catalog.json"]["assets"] = {f"{n}.landmark": {"mesh": f"{n}.glb"} for n in LEVELS}
    for name in LEVELS:
        data[f"levels/{name}.json"] = dict(target_level(static_spec, name), ready=False)
    return data


def create_project(root, spec):
    root.mkdir(parents=True)
    for name, value in initial_files(spec).items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        write_json(root / name, value)


def requirement_checks(root, spec, requirement_ids=None):
    """Read real candidate files. A worker's own success/evidence is irrelevant."""
    required = (
        {r["id"] for r in spec["requirements"]} if requirement_ids is None else set(requirement_ids)
    )
    checks = []
    for identifier in sorted(required):
        try:
            if identifier.startswith("level-"):
                name = identifier.removeprefix("level-")
                actual = read_json(root / f"levels/{name}.json")
                passed = actual == target_level(spec, name)
            elif identifier.startswith("asset-"):
                name = identifier.removeprefix("asset-")
                actual = read_json(root / "catalog.json")
                passed = actual.get("schema") == "eve.benchmark.catalog/v1" and actual.get(
                    "assets", {}
                ).get(f"{name}.landmark") == {"mesh": f"{name}.glb"}
            elif identifier == "world-links":
                actual = read_json(root / "world.json")
                # Local world validity does not prove the other agents exported
                # matching ports. The final graph check owns that invariant.
                passed = (
                    actual.get("schema") == "eve.benchmark.world/v1"
                    and actual.get("spawn") == "village"
                    and actual.get("destination") == "mine"
                    and len(actual.get("connections", [])) == 2
                    and all(
                        c.get("from") == a
                        and c.get("to") == b
                        and isinstance(c.get("exit"), str)
                        and bool(c["exit"])
                        and isinstance(c.get("entry"), str)
                        and bool(c["entry"])
                        for c, a, b in zip(actual["connections"], LEVELS, LEVELS[1:])
                    )
                )
            else:
                raise ValueError("unknown requirement")
            checks.append({"id": identifier, "passed": bool(passed)})
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
            checks.append({"id": identifier, "passed": False, "error": str(exc)})
    return checks


def integration_checks(root, spec):
    checks = requirement_checks(root, spec)
    try:
        world = read_json(root / "world.json")
        assets = read_json(root / "catalog.json")
        levels = {n: read_json(root / f"levels/{n}.json") for n in LEVELS}
        links_ok = world == target_world(spec) and all(
            c["exit"] == levels[c["from"]]["exit"] and c["entry"] == levels[c["to"]]["entry"]
            for c in world["connections"]
        )
        refs_ok = assets.get("schema") == "eve.benchmark.catalog/v1" and assets.get("assets") == {
            f"{n}.landmark": {"mesh": f"{n}.glb"} for n in LEVELS
        }
        checks.extend(
            [
                {"id": "connected-campaign", "passed": links_ok},
                {"id": "asset-references-and-retention", "passed": refs_ok},
            ]
        )
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        checks.append({"id": "integration-readable", "passed": False, "error": str(exc)})
    return checks


def validate_plan(plan, spec, max_tasks=8):
    """Validate executable ownership/DAG and report decomposition tradeoffs separately."""
    if not isinstance(plan, dict) or plan.get("schema") != "eve.agent-team-plan/v1":
        return {"valid": False, "errors": ["unknown plan schema"]}
    tasks = plan.get("tasks")
    if not isinstance(tasks, list) or not 2 <= len(tasks) <= max_tasks:
        return {"valid": False, "errors": ["plan needs 2..8 productive tasks"]}
    errors = []
    reqs = {r["id"]: r for r in spec["requirements"]}
    ids = [t.get("id") for t in tasks if isinstance(t, dict)]
    if (
        len(ids) != len(tasks)
        or any(
            not isinstance(i, str) or not i.isascii() or not i.replace("-", "").isalnum()
            for i in ids
        )
        or len(set(ids)) != len(ids)
    ):
        return {"valid": False, "errors": ["invalid/duplicate task ids"]}
    owners = {}
    deps = {}
    files = {}
    for task in tasks:
        identifier = task["id"]
        assigned = task.get("requirements", [])
        allowed = task.get("allowedFiles", [])
        dependency = task.get("dependsOn", [])
        if (
            not isinstance(assigned, list)
            or not assigned
            or not all(isinstance(r, str) and r in reqs for r in assigned)
        ):
            errors.append(f"{identifier}: unknown/empty requirements")
            continue
        for requirement in assigned:
            if requirement in owners:
                errors.append(f"duplicate ownership: {requirement}")
            owners[requirement] = identifier
        if (
            not isinstance(allowed, list)
            or not allowed
            or not all(isinstance(p, str) and p in FILES for p in allowed)
            or not set(p for r in assigned for p in reqs[r]["files"]) <= set(allowed)
        ):
            errors.append(f"{identifier}: invalid/incomplete allowedFiles")
            continue
        if not isinstance(dependency, list) or not all(
            isinstance(d, str) and d in ids and d != identifier for d in dependency
        ):
            errors.append(f"{identifier}: invalid dependency")
            continue
        deps[identifier] = set(dependency)
        files[identifier] = set(allowed)
    missing = sorted(set(reqs) - owners.keys())
    if missing:
        errors.append(f"unowned requirements: {missing}")
    if len(deps) != len(tasks):
        return {"valid": False, "errors": errors}
    remaining, done, waves, ancestors = set(ids), set(), [], {}
    depths = {}
    while remaining:
        ready = sorted(i for i in remaining if deps[i] <= done)
        if not ready:
            errors.append("dependency cycle")
            break
        waves.append(ready)
        for identifier in ready:
            ancestors[identifier] = set(deps[identifier])
            for d in deps[identifier]:
                ancestors[identifier].update(ancestors[d])
            depths[identifier] = 1 + max((depths[d] for d in deps[identifier]), default=0)
        done.update(ready)
        remaining.difference_update(ready)
    missing_edges = []
    if not remaining:
        for requirement in reqs.values():
            for upstream in requirement["dependsOn"]:
                a, b = owners.get(upstream), owners.get(requirement["id"])
                if a and b and a != b and a not in ancestors[b]:
                    missing_edges.append([a, b])
        if missing_edges:
            errors.append("required dependency/handoff omitted")
    overlap = [
        [a, b, sorted(files[a] & files[b])]
        for index, a in enumerate(ids)
        for b in ids[index + 1 :]
        if files[a] & files[b]
    ]
    ownership = Counter(owners.values())
    return {
        "valid": not errors,
        "errors": errors,
        "taskCount": len(tasks),
        "requirementCoverage": len(owners) / len(reqs),
        "missingDependencyEdges": missing_edges,
        "writeOverlapPairs": overlap,
        "declaredFileExposures": sum(map(len, files.values())),
        "maxRunnableWidth": max(map(len, waves), default=0),
        "criticalPathTaskCount": max(depths.values(), default=0),
        "handoffEdges": sum(map(len, deps.values())),
        "largestTaskRequirementFraction": max(ownership.values(), default=0) / len(reqs),
    }


def reference_plan(spec):
    tasks = []
    for index, name in enumerate(LEVELS):
        tasks.append(
            {
                "id": name,
                "requirements": [f"level-{name}"]
                + ([f"asset-{name}"] if spec["scenario"] == "shared-catalog" else []),
                "allowedFiles": [f"levels/{name}.json"]
                + (["catalog.json"] if spec["scenario"] == "shared-catalog" else []),
                "dependsOn": [LEVELS[index - 1]]
                if spec["scenario"] == "dependency-chain" and index
                else [],
            }
        )
    if spec["scenario"] == "dependency-chain":
        tasks.append(
            {
                "id": "world",
                "requirements": ["world-links"],
                "allowedFiles": ["world.json"],
                "dependsOn": list(LEVELS),
            }
        )
    return {"schema": "eve.agent-team-plan/v1", "tasks": tasks}


def reference_edit(root, spec, assigned):
    for identifier in assigned:
        if identifier.startswith("level-"):
            name = identifier.removeprefix("level-")
            write_json(root / f"levels/{name}.json", target_level(spec, name))
        elif identifier.startswith("asset-"):
            name = identifier.removeprefix("asset-")
            catalog = read_json(root / "catalog.json")
            catalog["assets"][f"{name}.landmark"] = {"mesh": f"{name}.glb"}
            write_json(root / "catalog.json", catalog)
        else:
            write_json(root / "world.json", target_world(spec))


def reference_integrate(root, spec):
    write_json(root / "world.json", target_world(spec))
    write_json(
        root / "catalog.json",
        {
            "schema": "eve.benchmark.catalog/v1",
            "assets": {f"{n}.landmark": {"mesh": f"{n}.glb"} for n in LEVELS},
        },
    )
