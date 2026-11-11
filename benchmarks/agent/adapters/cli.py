#!/usr/bin/env python3
"""Noninteractive CLI adapter; stdout is retained raw, never used for acceptance."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import read_json, write_json  # noqa: E402


def codex_usage(events):
    usage = {"inputTokens": 0, "outputTokens": 0, "toolCalls": 0, "humanInterventions": 0}
    turns = 0
    for event in events:
        if event.get("type") == "turn.completed":
            counts = event["usage"]
            usage["inputTokens"] += counts["input_tokens"]
            usage["outputTokens"] += counts["output_tokens"]
            turns += 1
        if event.get("type") == "item.completed" and event.get("item", {}).get("type") in (
            "command_execution",
            "mcp_tool_call",
            "web_search",
        ):
            usage["toolCalls"] += 1
    if not turns:
        usage["inputTokens"] = usage["outputTokens"] = None
    return usage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("codex", "deepseek"), required=True)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--model")
    parser.add_argument(
        "--codex-config",
        action="append",
        default=[],
        help="Explicit Codex KEY=TOML override, including MCP tools; recorded in adapter config",
    )
    parser.add_argument(
        "--cli-command-json",
        help="DeepSeek noninteractive argv; prompt is sent on stdin. Pin exact CLI and flags in adapter config.",
    )
    args = parser.parse_args()
    request = read_json(args.request)
    artifacts = Path(request["artifacts"])
    prompt = (
        f"Complete this EVEngine benchmark task independently.\n{request['prompt']}\n"
        f"Workspace: {request['workspace']}\nAllowed edits: {', '.join(request['allowedFiles'])}\n"
        f"Rules: {request['rules']}\nRuntime case seed: {request['seed']}. "
        "This is not a model sampling seed. Do not create extra files in the workspace.\n"
        f"Engine executable (if configured): {request.get('engine')}\n"
        "Inspect main.nut and game.agent.json before editing. The independent evaluator runs after you finish.\n"
    )
    if request.get("role"):
        prompt = (
            f"EVEngine collaboration benchmark. Role: {request['role']}\n{request['prompt']}\n"
            f"Workspace: {request['workspace']}\nAllowed edits: {request['allowedFiles']}\n"
            f"Rules: {request['rules']}\n"
            + json.dumps(
                {
                    key: request[key]
                    for key in (
                        "projectSpec",
                        "task",
                        "dependencyOutputs",
                        "baselinePath",
                        "workspaceTargets",
                    )
                    if key in request
                },
                ensure_ascii=False,
            )
            + "\nRead the existing project JSON before editing. Independent validation follows."
        )
    if args.backend == "codex":
        command = [
            "codex",
            "--no-daemon",
            "-a",
            "never",
            "exec",
            "--ignore-user-config",
            "--ephemeral",
            "--skip-git-repo-check",
            "--sandbox",
            "workspace-write",
            "--json",
            "-C",
            request["workspace"],
        ]
        if args.model:
            command.extend(["--model", args.model])
        for override in args.codex_config:
            command.extend(["-c", override])
        command.append("-")
    else:
        if not args.cli_command_json:
            parser.error(
                "DeepSeek CLI implementations differ: --cli-command-json must specify a noninteractive command accepting stdin"
            )
        command = json.loads(args.cli_command_json)
        if (
            not isinstance(command, list)
            or not command
            or not all(isinstance(p, str) for p in command)
        ):
            parser.error("--cli-command-json must be a nonempty argv array")
    if not shutil.which(command[0]):
        parser.error(f"CLI executable unavailable: {command[0]}")
    version = subprocess.run(
        [command[0], "--version"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=10,
    )
    (artifacts / "cli-version.txt").write_text(version.stdout + version.stderr, encoding="utf-8")
    # State and credentials live outside published artifacts. The runner owns the
    # outer temporary directory and removes it even if this adapter times out.
    failure_kind = None
    with tempfile.TemporaryDirectory(prefix="cli-state-") as state:
        env = dict(os.environ)
        if args.backend == "codex":
            source_home = Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex")))
            auth = source_home / "auth.json"
            if auth.is_file():
                target = Path(state) / "auth.json"
                shutil.copyfile(auth, target)
                target.chmod(0o600)
            env["CODEX_HOME"] = state
        with (
            (artifacts / "events.jsonl").open("w", encoding="utf-8") as out,
            (artifacts / "stderr.txt").open("w", encoding="utf-8") as err,
        ):
            process = subprocess.Popen(
                command,
                cwd=request["workspace"],
                stdin=subprocess.PIPE,
                stdout=out,
                stderr=err,
                text=True,
                encoding="utf-8",
                env=env,
            )
            pending_input = prompt
            while True:
                try:
                    process.communicate(input=pending_input, timeout=1)
                    break
                except subprocess.TimeoutExpired:
                    pending_input = None
                    diagnostic = (artifacts / "stderr.txt").read_text(
                        encoding="utf-8", errors="replace"
                    )[-65536:]
                    if "HTTP CONNECT failed with status 403" in diagnostic:
                        failure_kind = "provider_policy_denied"
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                        break
    events = []
    if args.backend == "codex":
        for line in (artifacts / "events.jsonl").read_text(encoding="utf-8").splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    usage = codex_usage(events) if args.backend == "codex" else {}
    write_json(
        args.result,
        {
            "schema": "eve.agent-benchmark-adapter-result/v1",
            "usage": usage,
            "backend": args.backend,
            "requestedModel": args.model,
            "failureKind": failure_kind,
            "telemetrySource": "codex-jsonl" if args.backend == "codex" else "unavailable",
            "command": command,
        },
    )
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
