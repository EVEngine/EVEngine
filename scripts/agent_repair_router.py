#!/usr/bin/env python3
"""Route opted-in, same-repository PR failures to a GitHub-backed repair queue."""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from utf8_stdio import enable_utf8_stdio


MARKER = "<!-- evengine-agent-repair-state -->"
OPT_IN_LABEL = "agent:auto-fix"
NEEDED_LABEL = "agent:repair-needed"
CLAIMED_LABEL = "agent:repair-claimed"
EXHAUSTED_LABEL = "agent:repair-exhausted"
OWNER_PREFIX = "agent:owner:"
PROVIDER_PREFIX = "agent:provider:"
SUPPORTED_PROVIDERS = {"codex", "deepseek"}
FAILED_CONCLUSIONS = {"failure", "timed_out", "cancelled", "action_required"}


@dataclass(frozen=True)
class Decision:
    eligible: bool
    reason: str
    source_key: str = ""


def label_value(labels: set[str], prefix: str) -> str | None:
    values = sorted(label[len(prefix) :] for label in labels if label.startswith(prefix))
    return values[0] if len(values) == 1 and values[0] else None


def decide(event_name: str, event: dict[str, Any], pr: dict[str, Any], repository: str) -> Decision:
    labels = {item["name"] for item in pr.get("labels", [])}
    if pr.get("state") != "open":
        return Decision(False, "PR is not open")
    if pr.get("head", {}).get("repo", {}).get("full_name") != repository:
        return Decision(False, "fork PRs are never eligible")
    if OPT_IN_LABEL not in labels:
        return Decision(False, f"missing explicit opt-in label {OPT_IN_LABEL}")
    owner = label_value(labels, OWNER_PREFIX)
    provider = label_value(labels, PROVIDER_PREFIX)
    if not owner:
        return Decision(False, "exactly one non-empty agent:owner:* label is required")
    if provider not in SUPPORTED_PROVIDERS:
        return Decision(False, "exactly one supported agent:provider:* label is required")

    if event_name == "workflow_run":
        run = event.get("workflow_run", {})
        if run.get("name") != "CI" or run.get("event") != "pull_request":
            return Decision(False, "only PR runs of CI are routed")
        if run.get("conclusion") not in FAILED_CONCLUSIONS:
            return Decision(False, "CI did not fail")
        if run.get("head_sha") != pr.get("head", {}).get("sha"):
            return Decision(False, "workflow run is stale for the current PR head")
        return Decision(True, "CI failed", f"workflow_run:{run.get('id')}")

    if event_name == "pull_request_review":
        review = event.get("review", {})
        if review.get("state", "").lower() != "changes_requested":
            return Decision(False, "review did not request changes")
        if event.get("pull_request", {}).get("head", {}).get("sha") != pr.get("head", {}).get("sha"):
            return Decision(False, "review event is stale for the current PR head")
        return Decision(True, "review requested changes", f"review:{review.get('id')}")

    return Decision(False, "metadata event only; waiting for failed CI or requested changes")


def parse_state(body: str) -> dict[str, Any] | None:
    if MARKER not in body:
        return None
    try:
        payload = body.split(MARKER, 1)[1].strip()
        if payload.startswith("```json") and payload.endswith("```"):
            payload = payload[7:-3].strip()
        value = json.loads(payload)
        if isinstance(value, dict) and value.get("schema") == "evengine.agent-repair/v1":
            return value
        return None
    except (ValueError, TypeError):
        return None


def render_state(state: dict[str, Any]) -> str:
    summary = (
        f"Agent repair request is **{state['status']}** for `{state['head_sha'][:12]}` "
        f"(attempt {state['attempt']}/{state['max_attempts']}, provider `{state['provider']}`, "
        f"owner `{state['owner']}`)."
    )
    return f"{summary}\n\n{MARKER}\n```json\n{json.dumps(state, sort_keys=True)}\n```"


def next_attempt(previous_attempt: int, max_attempts: int) -> tuple[int, str]:
    candidate = previous_attempt + 1
    if candidate > max_attempts:
        return max_attempts, "exhausted"
    return candidate, "pending"


class GitHub:
    def __init__(self, api_url: str, repository: str, token: str) -> None:
        self.base = f"{api_url}/repos/{repository}"
        self.token = token

    def request(self, method: str, path: str, payload: Any | None = None) -> Any:
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            self.base + path,
            data=data,
            method=method,
            headers={
                "Accept": "application/vnd.github+json",
                "Authorization": f"Bearer {self.token}",
                "X-GitHub-Api-Version": "2022-11-28",
                "User-Agent": "evengine-agent-repair-router",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=30) as response:
                raw = response.read()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"GitHub API {method} {path} failed: {exc.code} {detail}") from exc


def event_pr_number(event_name: str, event: dict[str, Any]) -> int | None:
    if event_name == "workflow_run":
        prs = event.get("workflow_run", {}).get("pull_requests", [])
        return int(prs[0]["number"]) if len(prs) == 1 else None
    pr = event.get("pull_request")
    return int(pr["number"]) if pr else None


def latest_ci_run(gh: GitHub, pr: dict[str, Any]) -> dict[str, Any] | None:
    branch = urllib.parse.quote(pr["head"]["ref"], safe="")
    runs = gh.request("GET", f"/actions/runs?event=pull_request&branch={branch}&per_page=100")
    matching = [
        run
        for run in runs.get("workflow_runs", [])
        if run.get("name") == "CI" and run.get("head_sha") == pr["head"]["sha"]
    ]
    return max(matching, key=lambda run: run.get("run_number", 0), default=None)


def cancel_existing(gh: GitHub, pr_number: int, reason: str) -> None:
    comments = gh.request("GET", f"/issues/{pr_number}/comments?per_page=100")
    existing = next(((comment, parse_state(comment.get("body", ""))) for comment in comments if parse_state(comment.get("body", ""))), None)
    if not existing or existing[1].get("status") not in {"pending", "claimed"}:
        return
    state = dict(existing[1], status="cancelled", cancel_reason=reason)
    gh.request("PATCH", f"/issues/comments/{existing[0]['id']}", {"body": render_state(state)})
    for stale in (NEEDED_LABEL, CLAIMED_LABEL):
        try:
            gh.request("DELETE", f"/issues/{pr_number}/labels/{stale}")
        except RuntimeError as exc:
            if "404" not in str(exc):
                raise


def ensure_label(gh: GitHub, name: str, color: str, description: str) -> None:
    try:
        gh.request("POST", "/labels", {"name": name, "color": color, "description": description})
    except RuntimeError as exc:
        if "already_exists" not in str(exc):
            raise


def main() -> int:
    event_name = os.environ["REPAIR_EVENT_NAME"]
    repository = os.environ["REPAIR_REPOSITORY"]
    event = json.loads(Path(os.environ["REPAIR_EVENT_PATH"]).read_text(encoding="utf-8"))
    pr_number = event_pr_number(event_name, event)
    if pr_number is None:
        print("skip: event does not identify exactly one PR")
        return 0

    gh = GitHub(os.environ.get("REPAIR_API_URL", "https://api.github.com"), repository, os.environ["GH_TOKEN"])
    pr = gh.request("GET", f"/pulls/{pr_number}")
    if event_name == "pull_request_target" and event.get("action") in {"labeled", "reopened"}:
        run = latest_ci_run(gh, pr)
        if run:
            event_name = "workflow_run"
            event = {"workflow_run": run}
    decision = decide(event_name, event, pr, repository)
    print(f"PR #{pr_number}: {decision.reason}")
    if not decision.eligible:
        action = event.get("action")
        current_run = event.get("workflow_run", {})
        removed_label = event.get("label", {}).get("name", "")
        routing_label_removed = action == "unlabeled" and (
            removed_label == OPT_IN_LABEL
            or removed_label.startswith(OWNER_PREFIX)
            or removed_label.startswith(PROVIDER_PREFIX)
        )
        should_cancel = (
            action in {"closed", "synchronize"}
            or routing_label_removed
            or (
                event_name == "workflow_run"
                and current_run.get("head_sha") == pr.get("head", {}).get("sha")
                and current_run.get("conclusion") == "success"
            )
        )
        if should_cancel:
            cancel_existing(gh, pr_number, decision.reason)
        return 0

    comments = gh.request("GET", f"/issues/{pr_number}/comments?per_page=100")
    existing = next(((c, parse_state(c.get("body", ""))) for c in comments if parse_state(c.get("body", ""))), None)
    previous = existing[1] if existing else {}
    if previous.get("source_key") == decision.source_key:
        print("skip: repair request already exists for this source event")
        return 0

    max_attempts = max(1, int(os.environ.get("REPAIR_MAX_ATTEMPTS", "3")))
    attempt, status = next_attempt(int(previous.get("attempt", 0)), max_attempts)
    labels = {item["name"] for item in pr.get("labels", [])}
    owner = label_value(labels, OWNER_PREFIX)
    provider = label_value(labels, PROVIDER_PREFIX)
    state = {
        "schema": "evengine.agent-repair/v1",
        "status": status,
        "pr": pr_number,
        "head_sha": pr["head"]["sha"],
        "owner": owner,
        "provider": provider,
        "attempt": attempt,
        "max_attempts": max_attempts,
        "source_key": decision.source_key,
        "reason": decision.reason,
    }

    for name, color, description in (
        (NEEDED_LABEL, "d93f0b", "Queued for an opted-in local repair agent"),
        (CLAIMED_LABEL, "fbca04", "Claimed by a local repair agent"),
        (EXHAUSTED_LABEL, "5319e7", "Automatic repair attempt limit reached"),
    ):
        ensure_label(gh, name, color, description)
    target_label = EXHAUSTED_LABEL if status == "exhausted" else NEEDED_LABEL
    gh.request("POST", f"/issues/{pr_number}/labels", {"labels": [target_label]})
    for stale in ({CLAIMED_LABEL, EXHAUSTED_LABEL, NEEDED_LABEL} - {target_label}):
        try:
            gh.request("DELETE", f"/issues/{pr_number}/labels/{stale}")
        except RuntimeError as exc:
            if "404" not in str(exc):
                raise
    body = render_state(state)
    if existing:
        gh.request("PATCH", f"/issues/comments/{existing[0]['id']}", {"body": body})
    else:
        gh.request("POST", f"/issues/{pr_number}/comments", {"body": body})
    print(f"repair request {status}: attempt {attempt}/{max_attempts}")
    return 0


if __name__ == "__main__":
    enable_utf8_stdio()
    raise SystemExit(main())
