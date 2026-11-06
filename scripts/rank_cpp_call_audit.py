#!/usr/bin/env python3
"""Rank an existing semantic call audit for manual review, without rerunning Clang."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
WEIGHTS = {
    "performance-unnecessary-copy-initialization": 40,
    "performance-for-range-copy": 40,
    "performance-unnecessary-value-param": 35,
    "performance-no-automatic-move": 35,
    "performance-move-const-arg": 30,
    "performance-use-std-move": 30,
    "performance-inefficient-vector-operation": 35,
    "performance-inefficient-string-concatenation": 25,
    "bugprone-use-after-move": 50,
    "performance-enum-size": 2,
    "performance-avoid-endl": 8,
    "modernize-use-emplace": 10,
}


def finding_id(finding: dict) -> str:
    """Identify one diagnostic; changed locations require a new review."""
    identity = [finding[k] for k in ("file", "line", "column", "rule", "message")]
    return hashlib.sha256(json.dumps(identity, ensure_ascii=False).encode("utf-8")).hexdigest()


def declared_hot_path(path: Path, root: Path) -> bool:
    """Use repository declarations, never a filename-based frequency guess."""
    if re.search(r"^\s*//\s*eve-hot-path\b", path.read_text(encoding="utf-8"), re.M):
        return True
    for parent in path.parents:
        if not parent.is_relative_to(root):
            break
        policy = parent / ".eve-hot-path"
        if policy.is_file() and "eve.hot-path-directory/v1:*.cpp" in policy.read_text(
            encoding="utf-8"
        ):
            return path.suffix == ".cpp"
    return False


def rank_report(report: dict, root: Path, decisions: list[dict] = ()) -> dict:
    """Return an explainable review queue; scores are heuristics, not measured ROI."""
    semantic = report["clang_tidy"]
    indexed = {}
    for decision in decisions:
        identity = decision["finding_id"]
        if identity in indexed:
            raise ValueError(f"Duplicate review decision: {identity}")
        if (
            decision["status"] not in {"retained", "confirmed", "needs-measurement"}
            or not decision["reason"].strip()
        ):
            raise ValueError("Review decisions need a supported status and a nonempty reason")
        indexed[identity] = decision
    rows, used = [], set()
    sources = {}
    for finding in semantic["findings"]:
        path = (root / finding["file"]).resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError("Diagnostic source escapes repository")
        if path not in sources:
            sources[path] = (
                (
                    hashlib.sha256(path.read_bytes()).hexdigest(),
                    declared_hot_path(path, root.resolve()),
                )
                if path.is_file()
                else (None, False)
            )
        digest, hot = sources[path]
        score = WEIGHTS.get(finding["rule"], 15)
        reasons = [f"rule potential: {score}"]
        message = finding["message"]
        if "function<" in message:
            score += 20
            reasons.append("owning callable: +20; independent mutable state may require copies")
        elif re.search(r"(?:vector|map|unordered_map|list|deque|set)<", message):
            score += 35
            reasons.append("owning container type: +35; size and caller copies still unproven")
        elif "basic_string" in message or "std::string" in message:
            score += 10
            reasons.append("owning string: +10; SSO and length unknown")
        if "SharedPtr" in message or "shared_ptr" in message:
            score -= 25
            reasons.append("shared ownership handle: -25; pointee is not deep copied")
        if "trivially copyable" in message:
            score -= 30
            reasons.append("trivial copy: -30")
        if hot:
            score += 15
            reasons.append("declared hot-path source: +15; invocation count remains unmeasured")
        verified = bool(finding.get("verified_translation_units"))
        if not verified or digest is None:
            score = 0
            reasons.append("no successful semantic evidence or missing current source: defer")
        identity = finding_id(finding)
        decision = indexed.get(identity)
        status = "unreviewed"
        if decision:
            used.add(identity)
            if digest is not None and decision["source_sha256"] == digest:
                status = decision["status"]
            else:
                reasons.append("review source changed: prior decision is stale")
        rows.append(
            {
                **finding,
                "finding_id": identity,
                "source_sha256": digest,
                "priority_score": max(score, 0),
                "priority_reasons": reasons,
                "review_status": status,
                "review_reason": decision["reason"] if decision else None,
                "frequency_evidence": "declared-hot-path" if hot else "unknown",
                "benefit_evidence": "unmeasured",
            }
        )
    rows.sort(
        key=lambda x: (
            x["review_status"] == "retained",
            not bool(x.get("verified_translation_units")),
            x["review_status"] != "confirmed",
            -x["priority_score"],
            x["file"],
            x["line"],
            x["column"],
            x["rule"],
        )
    )
    return {
        "schema": "evengine.cpp-call-audit-priority",
        "version": 1,
        "unknown_fields": "ignore",
        "limitations": "Heuristic review order only, not measured savings or proof that a change is safe. "
        "Review ownership, snapshots, reentrancy, actual callers and allocations before fixes.",
        "coverage": {k: semantic[k] for k in ("analyzed_units", "failed_units", "missing_units")},
        "counts": dict(Counter(row["review_status"] for row in rows)),
        "unmatched_decisions": sorted(set(indexed) - used),
        "queue": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument(
        "--decisions", type=Path, help="JSON list of source-hash-bound review decisions"
    )
    parser.add_argument("--output", type=Path, default=Path("build/cpp-call-audit-priority.json"))
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()
    if args.top < 0:
        parser.error("--top must be nonnegative")
    report = json.loads(args.report.read_text(encoding="utf-8-sig"))
    decisions = json.loads(args.decisions.read_text(encoding="utf-8-sig")) if args.decisions else []
    ranked = rank_report(report, ROOT, decisions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(ranked, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    active = [row for row in ranked["queue"] if row["review_status"] != "retained"]
    for row in active[: args.top]:
        print(
            f"{row['file']}:{row['line']}: score={row['priority_score']} "
            f"[{row['review_status']}] {row['rule']} {row['message']}"
        )
    print(f"{len(active)} active; review states: {ranked['counts']}; report: {args.output}")
    return 2 if ranked["coverage"]["failed_units"] or ranked["coverage"]["missing_units"] else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
