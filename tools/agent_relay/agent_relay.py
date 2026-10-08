#!/usr/bin/env python3
"""Poll and dispatch GitHub-backed EVEngine agent repair requests."""

from __future__ import annotations

import argparse
import hashlib
import hmac
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
SIGNED_FIELDS = (
    "schema",
    "status",
    "repository",
    "pr",
    "head_sha",
    "owner",
    "provider",
    "attempt",
    "max_attempts",
    "source_key",
    "reason",
)


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


def signature_payload(state: dict[str, Any]) -> bytes:
    payload = {field: state.get(field) for field in SIGNED_FIELDS}
    return json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")


def valid_state(state: dict[str, Any], key: str, repository: str, pr_number: int) -> bool:
    signature = state.get("signature")
    if not isinstance(signature, str):
        return False
    expected = hmac.new(key.encode("utf-8"), signature_payload(state), hashlib.sha256).hexdigest()
    return (
        hmac.compare_digest(signature, expected)
        and state.get("repository") == repository
        and state.get("pr") == pr_number
    )


def sign_state(state: dict[str, Any], key: str) -> dict[str, Any]:
    signed = dict(state)
    signed["signature"] = hmac.new(key.encode("utf-8"), signature_payload(state), hashlib.sha256).hexdigest()
    return signed


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
        return self.get_all(f"/issues?state=open&labels={label}")

    def get_all(self, path: str) -> list[dict[str, Any]]:
        separator = "&" if "?" in path else "?"
        items: list[dict[str, Any]] = []
        page = 1
        while True:
            batch = self.request("GET", f"{path}{separator}per_page=100&page={page}")
            if not isinstance(batch, list):
                raise RuntimeError(f"GitHub API pagination expected a list for {path}")
            items.extend(batch)
            if len(batch) < 100:
                return items
            page += 1

    def acquire_lease(self, ref: str, sha: str) -> bool:
        try:
            self.request("POST", "/git/refs", {"ref": f"refs/heads/{ref}", "sha": sha})
            return True
        except RuntimeError as exc:
            if "422" in str(exc):
                return False
            raise

    def ref_exists(self, ref: str) -> bool:
        try:
            self.request("GET", f"/git/ref/heads/{urllib.parse.quote(ref, safe='/')}")
            return True
        except RuntimeError as exc:
            if "404" in str(exc):
                return False
            raise



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


def find_state(
    gh: GitHub, number: int, signing_key: str
) -> tuple[dict[str, Any], dict[str, Any]] | None:
    matches = []
    comments = gh.get_all(f"/issues/{number}/comments")
    for comment in comments:
        state = parse_state(comment.get("body", ""))
        if (
            comment.get("user", {}).get("login") == "github-actions[bot]"
            and state
            and valid_state(state, signing_key, gh.repository, number)
        ):
            matches.append((comment, state))
    return max(
        matches,
        key=lambda item: (int(item[1].get("attempt", 0)), int(item[0]["id"])),
        default=None,
    )


def remove_label(gh: GitHub, number: int, label: str) -> None:
    try:
        gh.request("DELETE", f"/issues/{number}/labels/{urllib.parse.quote(label)}")
    except RuntimeError as exc:
        if "404" not in str(exc):
            raise


def lease_ref(number: int, state: dict[str, Any], now: float, lease_seconds: int) -> tuple[str, float]:
    bucket = int(now // lease_seconds)
    expires_at = float((bucket + 1) * lease_seconds)
    ref = (
        f"agent-repair-leases/pr-{number}-{state['head_sha'][:12]}-"
        f"a{state['attempt']}-w{bucket}"
    )
    return ref, expires_at


def completion_ref(number: int, state: dict[str, Any]) -> str:
    source_digest = hashlib.sha256(str(state["source_key"]).encode("utf-8")).hexdigest()[:12]
    return (
        f"agent-repair-completions/pr-{number}-{state['head_sha'][:12]}-"
        f"a{state['attempt']}-{source_digest}"
    )


def process_one(
    gh: GitHub,
    issue: dict[str, Any],
    owner: str,
    relay_id: str,
    signing_key: str,
    lease_seconds: int,
    adapters: dict[str, Adapter],
    dry_run: bool,
) -> bool:
    number = int(issue["number"])
    found = find_state(gh, number, signing_key)
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

    completed_ref = completion_ref(number, state)
    if gh.ref_exists(completed_ref):
        logging.info("PR #%s request already reached a terminal dispatch state", number)
        remove_label(gh, number, NEEDED_LABEL)
        return False

    now = time.time()
    ref, expires_at = lease_ref(number, state, now, lease_seconds)
    if expires_at - now <= adapter.timeout_seconds + 60:
        logging.info("PR #%s lease window is too close to expiry; waiting for the next window", number)
        return False
    if not gh.acquire_lease(ref, state["head_sha"]):
        logging.info("PR #%s lease is owned by another relay", number)
        return False

    final_status = "dispatch_failed"
    diagnostic = "provider did not start"
    return_code: int | None = None
    try:
        fresh_pr = gh.request("GET", f"/pulls/{number}")
        if fresh_pr.get("head", {}).get("sha") != state.get("head_sha"):
            final_status = "cancelled"
            diagnostic = "head changed after lease acquisition"
        else:
            return_code = adapter.dispatch(build_prompt(gh.repository, fresh_pr, state), False)
            final_status = "dispatched" if return_code == 0 else "dispatch_failed"
            diagnostic = f"provider exited with code {return_code}"
    except subprocess.TimeoutExpired:
        final_status = "dispatch_timeout"
        diagnostic = f"provider exceeded {adapter.timeout_seconds} seconds"
        logging.exception("PR #%s provider timed out", number)
    except OSError as exc:
        final_status = "dispatch_failed"
        diagnostic = f"provider launch failed: {type(exc).__name__}: {exc}"
        logging.exception("PR #%s provider launch failed", number)
    finally:
        completed = sign_state(
            dict(
                state,
                status=final_status,
                relay_id=relay_id,
                lease_expires_at=int(expires_at),
                dispatch_exit_code=return_code,
                diagnostic=diagnostic,
            ),
            signing_key,
        )
        gh.acquire_lease(completed_ref, state["head_sha"])
        gh.request("PATCH", f"/issues/comments/{comment['id']}", {"body": render_state(completed)})
        remove_label(gh, number, NEEDED_LABEL)
    logging.info("PR #%s %s: %s", number, final_status, diagnostic)
    return True


def run_once(
    gh: GitHub,
    owner: str,
    relay_id: str,
    signing_key: str,
    lease_seconds: int,
    adapters: dict[str, Adapter],
    dry_run: bool,
) -> int:
    handled = 0
    for issue in gh.pending_prs():
        if "pull_request" not in issue:
            continue
        try:
            handled += int(
                process_one(gh, issue, owner, relay_id, signing_key, lease_seconds, adapters, dry_run)
            )
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
    signing_key = os.environ.get(config.get("signing_key_env", "AGENT_REPAIR_HMAC_KEY"), "")
    if not signing_key:
        raise SystemExit("repair signing key is missing; set the configured signing_key_env variable")
    owner = str(config["owner"])
    relay_id = str(config.get("relay_id") or f"{owner}@{socket.gethostname()}")
    gh = GitHub(str(config["repository"]), token, str(config.get("api_url", "https://api.github.com")))
    adapters = load_adapters(args.config)
    lease_seconds = int(config.get("lease_seconds", 3900))
    minimum_lease = max((adapter.timeout_seconds for adapter in adapters.values()), default=0) + 120
    if lease_seconds < minimum_lease:
        raise SystemExit(f"lease_seconds must be at least {minimum_lease} for the configured timeouts")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    while True:
        run_once(gh, owner, relay_id, signing_key, lease_seconds, adapters, args.dry_run)
        if args.once:
            return 0
        time.sleep(max(5, args.interval))


if __name__ == "__main__":
    raise SystemExit(main())
