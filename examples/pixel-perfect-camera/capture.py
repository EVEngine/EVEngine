#!/usr/bin/env python3
"""Capture engine MCP screenshots for the pixel-perfect-camera example."""

from __future__ import annotations

import json
import socket
import sys
import time
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7531
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("/opt/cursor/artifacts")


def call(name: str, args: dict | None = None):
    with socket.create_connection(("127.0.0.1", PORT), timeout=40) as sock:
        sock.sendall(
            (
                json.dumps(
                    {
                        "jsonrpc": "2.0",
                        "id": 1,
                        "method": "tools/call",
                        "params": {"name": name, "arguments": args or {}},
                    }
                )
                + "\n"
            ).encode()
        )
        raw = sock.makefile("rb").readline()
        reply = json.loads(raw)
        if "error" in reply or reply.get("result", {}).get("isError"):
            raise RuntimeError(reply)
        text = reply["result"]["content"][0]["text"]
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text


def capture(name: str) -> Path:
    path = OUT / f"{name}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    last = None
    for _ in range(8):
        last = call("eve_screenshot", {"path": str(path.resolve())})
        if isinstance(last, dict) and last.get("ok"):
            return path
        if isinstance(last, dict) and not last.get("retryable", True):
            break
        time.sleep(0.25)
    raise RuntimeError(f"screenshot failed for {name}: {last}")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    # Warm a few frames so the first present is complete.
    time.sleep(1.5)
    capture("pixel-perfect-camera-initial")
    # Nudge right a few times through script so the snapped pose is visible.
    call(
        "eve_run_script",
        {
            "source": "nudgeX += 0.9; placeCamera(); status = \"capture nudge x=\" + cam3.getEyeX();"
        },
    )
    time.sleep(0.4)
    capture("pixel-perfect-camera-nudged")
    print(json.dumps({"ok": True, "out": str(OUT)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
