#!/usr/bin/env python3
"""Poll and dispatch GitHub-backed EVEngine agent repair requests."""

from __future__ import annotations

import argparse
import json
import logging
import os
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

MARKER = "<!-- evengine-agent-repair-state -->"
NEEDED_LABEL = "agent:repair-needed"
CLAIMED_LABEL = "agent:repair-claimed"


def parse_state(body: str) -> dict[str, Any] | None:
    if MARKER not in body:
        return None
    try:
        payload = body.split(MARKER, 1)[1].strip()
        if payload.startswith("```json") and payload.endswith("```"):
            payload = payload[7:-3].strip()
        state = json.loads(payload)
        return state if state.get("schema") == "evengine.agent-repair/v1" else None
    except (ValueError, TypeError, AttributeError):
        return None


def render_state(state: dict[str, Any]) -> str:
    summary = (
        f"Agent repair request is **{state['status']}** for `{state['head_sha'][:12]}` "
        f"(attempt {state['attempt']}/{state['max_attempts']}, provider `{state['provider']}`, "
        f"owner `{state['owner']}`)."
    )
    return f"{summary}\n\n{MARKER}\n```json\n{json.dumps(state, sort_keys=True)}\n```"


class GitHub:
    def __init__(self, repository: str, token: str, api_url: str = "https://api.github.com") -> None:
        self.repository = repository
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
                "User-Agent": "evengine-agent-relay",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=30) as response:
                raw = response.read()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"GitHub API {method} {path} failed: {exc.code} {detail}") from exc

    def pending_prs(self) -> list[dict[str, Any]]:
        label = urllib.parse.quote(NEEDED_LABEL)
        return self.request("GET", f"/issues?state=open&labels={label}&per_page=100")


@dataclass(frozen=True)
class Adapter:
    provider: str
    command: list[str]
    cwd: Path
    timeout_seconds: int

    def dispatch(self, prompt: str, dry_run: bool) -> int:
        if not self.command:
            raise ValueError(f"provider {self.provider!r} has no configured command")
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".md", delete=False) as stream:
            stream.write(prompt)
            prompt_path = Path(stream.name)
        try:
            argv = [part.replace("{prompt_file}", str(prompt_path)) for part in self.command]
            uses_file = any("{prompt_file}" in part for part in self.command)
            logging.info("dispatch provider=%s argv[0]=%s cwd=%s", self.provider, argv[0], self.cwd)
            if dry_run:
                return 0
            completed = subprocess.run(
                argv,
                cwd=self.cwd,
                input=None if uses_file else prompt,
                text=True,
                shell=False,
                timeout=self.timeout_seconds,
                check=False,
            )
            return completed.returncode
        finally:
            prompt_path.unlink(missing_ok=True)


def load_adapters(config_path: Path) -> dict[str, Adapter]:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    adapters: dict[str, Adapter] = {}
    for provider in ("codex", "deepseek"):
        entry = data.get("providers", {}).get(provider, {})
        command = entry.get("command", [])
        if command and (not isinstance(command, list) or not all(isinstance(v, str) and v for v in command)):
            raise ValueError(f"providers.{provider}.command must be an array of non-empty strings")
        adapters[provider] = Adapter(
            provider,
            command,
            Path(entry.get("cwd", data.get("workspace", "."))).resolve(),
            int(entry.get("timeout_seconds", 3600)),
        )
    return adapters


def build_prompt(repository: str, pr: dict[str, Any], state: dict[str, Any]) -> str:
    return f"""Repair EVEngine PR #{pr['number']} in {repository}.

Expected head SHA: {state['head_sha']}
Repair attempt: {state['attempt']} of {state['max_attempts']}
Reason: {state['reason']}

Fetch and verify the PR head before editing. Diagnose the failing CI/review evidence, make the narrowest justified fix, run focused validation, commit, and push to the existing same-repository PR branch. Stop without pushing if the head SHA changed. Do not merge the PR and do not use a self-hosted GitHub Actions runner.
"""


def find_state(gh: GitHub, number: int) -> tuple[dict[str, Any], dict[str, Any]] | None:
    comments = gh.request("GET", f"/issues/{number}/comments?per_page=100")
    for comment in comments:
        state = parse_state(comment.get("body", ""))
        if state:
            return comment, state
    return None


def set_labels(gh: GitHub, number: int, add: str, remove: str) -> None:
    gh.request("POST", f"/issues/{number}/labels", {"labels": [add]})
    try:
        gh.request("DELETE", f"/issues/{number}/labels/{urllib.parse.quote(remove)}")
    except RuntimeError as exc:
        if "404" not in str(exc):
            raise


def process_one(
    gh: GitHub,
    issue: dict[str, Any],
    owner: str,
    relay_id: str,
    adapters: dict[str, Adapter],
    dry_run: bool,
) -> bool:
    number = int(issue["number"])
    found = find_state(gh, number)
    if not found:
        logging.warning("PR #%s has queue label but no valid state comment", number)
        return False
    comment, state = found
    if state.get("status") != "pending" or state.get("owner") != owner:
        return False
    adapter = adapters.get(str(state.get("provider")))
    if not adapter or not adapter.command:
        logging.warning("PR #%s provider %r is not configured", number, state.get("provider"))
        return False

    pr = gh.request("GET", f"/pulls/{number}")
    labels = {item["name"] for item in pr.get("labels", [])}
    expected_owner = f"agent:owner:{owner}"
    expected_provider = f"agent:provider:{state.get('provider')}"
    if pr.get("state") != "open" or "agent:auto-fix" not in labels:
        logging.warning("PR #%s is closed or no longer opted in", number)
        return False
    if expected_owner not in labels or expected_provider not in labels:
        logging.warning("PR #%s routing labels changed; refusing dispatch", number)
        return False
    if pr.get("head", {}).get("repo", {}).get("full_name") != gh.repository:
        logging.warning("PR #%s became a fork; refusing dispatch", number)
        return False
    if pr.get("head", {}).get("sha") != state.get("head_sha"):
        logging.info("PR #%s head changed before claim; waiting for router refresh", number)
        return False
    if dry_run:
        logging.info("dry-run: PR #%s would be claimed and dispatched via %s", number, state.get("provider"))
        return True

    claimed = dict(state, status="claimed", relay_id=relay_id)
    gh.request("PATCH", f"/issues/comments/{comment['id']}", {"body": render_state(claimed)})
    set_labels(gh, number, CLAIMED_LABEL, NEEDED_LABEL)

    # Optimistic claim: re-read the shared state and dispatch only if our relay still owns it.
    confirmed = find_state(gh, number)
    fresh_pr = gh.request("GET", f"/pulls/{number}")
    if not confirmed or confirmed[1].get("relay_id") != relay_id:
        logging.info("PR #%s claim lost to another relay", number)
        return False
    if fresh_pr.get("head", {}).get("sha") != state.get("head_sha"):
        logging.warning("PR #%s head changed after claim; refusing dispatch", number)
        return False

    return_code = adapter.dispatch(build_prompt(gh.repository, fresh_pr, state), False)
    final_status = "dispatched" if return_code == 0 else "dispatch_failed"
    completed = dict(claimed, status=final_status, dispatch_exit_code=return_code)
    gh.request("PATCH", f"/issues/comments/{comment['id']}", {"body": render_state(completed)})
    logging.info("PR #%s %s", number, final_status)
    return True


def run_once(gh: GitHub, owner: str, relay_id: str, adapters: dict[str, Adapter], dry_run: bool) -> int:
    handled = 0
    for issue in gh.pending_prs():
        if "pull_request" not in issue:
            continue
        try:
            handled += int(process_one(gh, issue, owner, relay_id, adapters, dry_run))
        except Exception:
            logging.exception("failed to process PR #%s", issue.get("number"))
    return handled


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--once", action="store_true", help="poll once and exit")
    parser.add_argument("--dry-run", action="store_true", help="inspect eligible work without claiming or launching")
    parser.add_argument("--interval", type=int, default=30, help="poll interval in seconds")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    token = os.environ.get(config.get("token_env", "GH_TOKEN"), "")
    if not token:
        raise SystemExit("GitHub token is missing; set the configured token_env variable")
    owner = str(config["owner"])
    relay_id = str(config.get("relay_id") or f"{owner}@{socket.gethostname()}")
    gh = GitHub(str(config["repository"]), token, str(config.get("api_url", "https://api.github.com")))
    adapters = load_adapters(args.config)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    while True:
        run_once(gh, owner, relay_id, adapters, args.dry_run)
        if args.once:
            return 0
        time.sleep(max(5, args.interval))


if __name__ == "__main__":
    raise SystemExit(main())
