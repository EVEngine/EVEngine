"""Versioned JSON and bounded process primitives for the Agent benchmark."""

from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import time


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fingerprint(root):
    """No links: graders must never follow candidate-controlled paths."""
    result = {}
    for path in sorted(Path(root).rglob("*")):
        if path.is_symlink():
            raise ValueError(f"symlink forbidden: {path.relative_to(root)}")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = digest(path)
    return result


def stop_process(process, grouped=True):
    if os.name == "posix":
        try:
            if grouped:
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
        except ProcessLookupError:
            pass
    else:
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    process.wait()


def run_process(command, cwd, timeout, log, env=None):
    start = time.monotonic()
    with Path(log).open("wb") as stream:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            stdout=stream,
            stderr=subprocess.STDOUT,
            start_new_session=os.name == "posix",
        )
        timed_out = False
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            code = None
        finally:
            # Also reap background descendants after a normally exited parent.
            stop_process(process)
    return {"exitCode": code, "timedOut": timed_out, "wallSeconds": time.monotonic() - start}


def usage_metrics(value):
    """Optional adapter-reported telemetry, never an independently measured cost."""
    if value.get("schema") != "eve.agent-benchmark-adapter-result/v1":
        raise ValueError("unknown adapter result schema")
    usage = value.get("usage", {})
    result = {}
    for key in ("inputTokens", "outputTokens", "costUsd", "toolCalls", "humanInterventions"):
        number = usage.get(key)
        if number is not None and (
            isinstance(number, bool)
            or not isinstance(number, (int, float))
            or not math.isfinite(number)
            or number < 0
        ):
            raise ValueError(f"invalid usage.{key}")
        result[key] = number
    return result
