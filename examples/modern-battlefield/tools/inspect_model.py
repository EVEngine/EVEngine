"""Stage one model in an isolated game and capture actual engine frames via MCP."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import uuid
import time


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind")
    parser.add_argument("--eve", default=os.environ.get("EVE", "eve"))
    parser.add_argument("--size", type=float, default=9)
    parser.add_argument("--height", type=float, default=1.6)
    parser.add_argument("--air", action="store_true")
    parser.add_argument("--static", action="store_true", help="Capture front/rear views of a fixed prop")
    parser.add_argument("--single", action="store_true", help="Capture only the requested view")
    parser.add_argument("--view", choices=["front", "top", "rear"], default="front")
    parser.add_argument("--port", type=int, default=7543)
    args = parser.parse_args()
    base = Path(__file__).resolve().parents[1]
    output = base / "screenshots"
    output.mkdir(exist_ok=True)
    cache = base / ".inspection-cache"
    cache.mkdir(exist_ok=True)
    scratch = cache / ("model-" + uuid.uuid4().hex)
    stage = Path(scratch)
    stage.mkdir()
    shutil.copy2(base / "assets.nut", stage / "assets.nut")
    shutil.copytree(base / "models", stage / "models")
    shutil.copy2(base / "inspection/main.nut", stage / "main.nut")
    shutil.copy2(base / "inspection/config.nut", stage / "config.nut")
    selection = {"kind": args.kind, "size": args.size, "height": args.height, "air": args.air, "view": args.view,
                 "stationary": args.static}
    fields = ",".join(f"{k}={json.dumps(v)}" for k, v in selection.items())
    (stage / "selection.nut").write_text(
        "export function selection() { return {" + fields + "}; }\n", encoding="utf-8"
    )
    log_path = output / f"{args.kind}.log"
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [args.eve, "run", "--debug", f"--mcp-port={args.port}", str(stage)],
            stdout=log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        sock = None
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))
                if "INSPECTION READY:" in log_path.read_text(encoding="utf-8", errors="replace"):
                    sock = socket.create_connection(("127.0.0.1", args.port), 5)
                    break
                time.sleep(0.25)
            if sock is None:
                raise RuntimeError("Inspection did not become ready: " + log_path.read_text(encoding="utf-8", errors="replace"))
            sock.settimeout(30)
            stream = sock.makefile("r", encoding="utf-8")
            counter = 0

            def call(name: str, arguments: dict) -> dict:
                nonlocal counter
                counter += 1
                message = {"jsonrpc": "2.0", "id": counter, "method": "tools/call",
                           "params": {"name": name, "arguments": arguments}}
                sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
                reply = json.loads(stream.readline())
                if "error" in reply:
                    raise RuntimeError(reply)
                return json.loads(reply["result"]["content"][0]["text"])

            suffixes = [""] if args.single else ["", "-rear" if args.static else "-articulated"]
            for suffix in suffixes:
                if suffix:
                    if args.static:
                        deadline = time.monotonic() + 30
                        while "INSPECTION REAR READY:" not in log_path.read_text(encoding="utf-8", errors="replace"):
                            if process.poll() is not None or time.monotonic() >= deadline:
                                raise RuntimeError("Rear view did not become ready")
                            time.sleep(0.2)
                        time.sleep(0.3)
                    else:
                        time.sleep(5)
                view_suffix = "" if args.view == "front" else "-" + args.view
                path = output / f"{args.kind}{view_suffix}{suffix}.png"
                for _ in range(12):
                    result = call("eve_screenshot", {"path": str(path)})
                    if result.get("ok"):
                        print(json.dumps({"kind": args.kind, "capture": str(path), "ok": True}))
                        break
                    if not result.get("retryable"):
                        raise RuntimeError(result)
                    time.sleep(0.3)
                else:
                    raise RuntimeError("Screenshot never became ready")
            text = log_path.read_text(encoding="utf-8", errors="replace")
            if any(marker in text for marker in ["eve_init failed", "frame error", "Run failed:", "Runtime error"]):
                raise RuntimeError(text)
        finally:
            if sock:
                sock.close()
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
    # The stage is a fresh UUID directory owned by this invocation only.
    if stage.resolve().parent != cache.resolve():
        raise RuntimeError("Inspection stage escaped its cache directory")
    shutil.rmtree(stage)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    main()
