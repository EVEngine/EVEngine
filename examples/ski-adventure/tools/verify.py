"""Exercise the shipped game through eve_play; save engine-owned evidence."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import uuid


class Session:
    def __init__(self, exe, project, output):
        self.output = output
        self.events = []
        self.sequence = 0
        self.sock = None
        self.stream = None
        self.log = (output / "runtime.log").open("w", encoding="utf-8")
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.process = subprocess.Popen(
            [str(exe), "run", "--debug", f"--mcp-port={port}", str(project)],
            cwd=exe.parent,
            stdout=self.log,
            stderr=self.log,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError("Engine exited during startup; inspect runtime.log")
                try:
                    self.sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                    break
                except OSError:
                    time.sleep(0.2)
            if self.sock is None:
                raise TimeoutError("Engine MCP startup timed out")
            self.sock.settimeout(45)
            self.stream = self.sock.makefile("r", encoding="utf-8")
            self.call(
                "initialize",
                {
                    "protocolVersion": "2024-11-05",
                    "clientInfo": {"name": "ski-acceptance", "version": "1"},
                    "capabilities": {},
                },
            )
        except BaseException:
            self.close()
            raise

    def call(self, method, params):
        self.sequence += 1
        request = {"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params}
        self.sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("Engine closed the MCP connection")
            response = json.loads(line)
            if response.get("id") == self.sequence:
                if "error" in response:
                    raise RuntimeError(str(response["error"]))
                return response["result"]

    def tool(self, name, arguments):
        response = self.call("tools/call", {"name": name, "arguments": arguments})
        texts = [c["text"] for c in response.get("content", []) if c.get("type") == "text"]
        result = json.loads("\n".join(texts)) if texts else response
        self.events.append({"tool": name, "arguments": arguments, "result": result})
        if response.get("isError") or (result.get("ok") is False and not result.get("retryable")):
            raise RuntimeError(str(result))
        return result

    def play(self, op, **kwargs):
        return self.tool(
            "eve_play",
            {
                "request": {
                    "schemaId": "evengine.play-request",
                    "schemaVersion": 1,
                    "op": op,
                    **kwargs,
                }
            },
        )

    def act(self, action):
        self.play("act", action=action)

    def observe(self):
        return self.play("observe", observation="ski")["state"]

    def until(self, predicate, timeout=15):
        deadline = time.monotonic() + timeout
        state = None
        while time.monotonic() < deadline:
            state = self.observe()
            if predicate(state):
                return state
            time.sleep(0.04)
        raise AssertionError(f"Timed out waiting for gameplay state: {state}")

    def screenshot(self, name):
        path = self.output / f"{name}.png"
        for _ in range(5):
            result = self.tool("eve_screenshot", {"path": str(path)})
            if result.get("ok") is not False and path.exists():
                data = path.read_bytes()
                assert data[:8] == b"\x89PNG\r\n\x1a\n", "Invalid screenshot"
                assert int.from_bytes(data[16:20], "big") == 1280
                assert int.from_bytes(data[20:24], "big") == 720
                return
            time.sleep(0.15)
        raise AssertionError(f"No engine readback for {name}")

    def close(self):
        if self.stream:
            self.stream.close()
        if self.sock:
            self.sock.close()
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
        self.log.close()
        (self.output / "events.json").write_text(
            json.dumps(self.events, indent=2), encoding="utf-8"
        )


def controls(session):
    state = session.until(lambda s: s["phase"] == "ready" and s["drawCalls"] > 0)
    session.screenshot("title")
    # Reopening on the same snow segment must regenerate native contacts.
    for _ in range(3):
        session.act("restart")
        session.until(lambda s: s["grounded"])
    initial = session.observe()
    session.until(lambda s: s["speed"] > initial["speed"] + 60)
    session.act("jump")
    airborne = session.until(lambda s: s["altitude"] > 50)
    session.act("jump")
    time.sleep(0.18)
    assert session.observe()["jumps"] == airborne["jumps"], "Mid-air double jump"
    session.screenshot("jump")
    landed = session.until(lambda s: s["grounded"])
    assert landed["worldY"] > initial["worldY"] + 20, "Track must descend to the right"
    session.act("pause")
    frozen = session.observe()
    session.act("jump")
    time.sleep(0.3)
    paused = session.observe()
    for key in ("tick", "distance", "worldY", "speed"):
        assert paused[key] == frozen[key], f"Pause changed {key}"
    assert not paused["jumpQueued"]
    session.act("pause")
    session.until(lambda s: s["tick"] > frozen["tick"])
    session.until(lambda s: s["crashes"] > 0)
    session.act("restart")
    session.act("tuck")
    fast = session.until(lambda s: s["speed"] > 400 and s["grounded"])
    session.act("untuck")
    session.act("brake")
    session.until(lambda s: s["speed"] < fast["speed"] * 0.8)
    session.act("restart")
    session.until(lambda s: s["grounded"])
    session.act("flip")
    session.act("jump")
    # A complete turn can land between two observations; scoring is authoritative.
    session.until(lambda s: s["rotation"] >= 6.29 or s["flips"] > 0)
    session.act("release")
    state = session.until(lambda s: s["flips"] > 0)
    session.screenshot("gameplay")
    session.act("restart")
    reset = session.observe()
    assert all(reset[k] == 0 for k in ("coins", "flips", "crashes", "jumps", "rampJumps"))
    print(
        "PASS controls: contact reset, downhill, jump, pause, collision, brake, backflip",
        flush=True,
    )
    return state


def course(session):
    session.act("restart")
    session.act("tuck")
    deadline = time.monotonic() + 300
    ramp_seen = False
    previous = session.observe()
    while time.monotonic() < deadline:
        state = session.observe()
        if state["rampJumps"] > previous["rampJumps"]:
            assert state["jumps"] == previous["jumps"], "Ramp used a player jump"
            if not ramp_seen:
                session.screenshot("natural-ramp")
                print("PASS natural ramp takeoff without jump input", flush=True)
            ramp_seen = True
        if state["finished"]:
            break
        if state["nextKind"] in ("rock", "tree") and (state["grounded"] or state["altitude"] < 18):
            # Leave enough lead for both a tree's height and the MCP round trip.
            threshold = max(220, min(460, state["velocityX"] * 0.6 + 60))
            if 45 < state["nextDistance"] < threshold:
                session.act("jump")
        elif state["nextKind"] == "ramp" and state["grounded"] and state["velocityX"] < 160:
            # After a crash the skier can lack energy to crest a lip; the normal
            # jump input lets the player recover rather than slide backwards.
            if state["nextDistance"] < 250:
                session.act("jump")
        previous = state
        time.sleep(0.04)
    assert state["phase"] == "won" and state["won"], f"Course failed: {state}"
    assert state["stage"] == 3 and state["distance"] >= 36000 and ramp_seen
    assert state["score"] == 1200 + state["coins"] * 25 + state["flips"] * 250
    session.screenshot("win")
    best = state["best"]
    session.act("restart")
    loss = session.until(lambda s: s["finished"], timeout=120)
    assert loss["phase"] == "lost" and not loss["won"] and loss["best"] >= best
    session.screenshot("loss")
    print("PASS course: physical ramps, three stages, scoring, win and avalanche loss", flush=True)
    return {"win": state, "loss": loss}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eve", default=os.environ.get("EVE"), required=not os.environ.get("EVE"))
    parser.add_argument("--output", type=Path, default=Path("build/ski-validation"))
    parser.add_argument("--suite", choices=("all", "controls", "course"), default="all")
    args = parser.parse_args()
    exe = Path(args.eve).resolve(strict=True)
    project = Path(__file__).resolve().parents[1]
    output = args.output.resolve() / (time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6])
    output.mkdir(parents=True)
    sources = [
        *project.glob("*.nut"),
        *project.glob("scripts/*.nut"),
        project / "game.agent.json",
        *project.glob("assets/*.png"),
        Path(__file__).resolve(),
    ]
    report = {
        "schemaId": "ski-adventure.acceptance",
        "schemaVersion": 1,
        "passed": False,
        "suite": args.suite,
        "engine": str(exe),
        "engineSha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
        "sources": {
            str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources
        },
    }
    print(f"Evidence: {output}", flush=True)
    session = None
    try:
        session = Session(exe, project, output)
        if args.suite in ("all", "controls"):
            report["controls"] = controls(session)
        if args.suite in ("all", "course"):
            report["course"] = course(session)
        report["passed"] = True
    except BaseException as error:
        report["error"] = str(error)
        raise
    finally:
        if session:
            session.close()
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
