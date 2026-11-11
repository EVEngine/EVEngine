"""Trusted graders. Candidate statements and adapter telemetry never determine success."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import time

from common import read_json, stop_process, write_json


class RpcFailure(RuntimeError):
    pass


class Client:
    def __init__(self, port, transcript):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(5)
        self.stream = self.sock.makefile("rb")
        self.sequence = 0
        self.transcript = transcript
        self.rpc(
            "initialize",
            {
                "protocolVersion": "2025-06-18",
                "clientInfo": {"name": "eve-benchmark-grader", "version": "1"},
            },
        )
        self.sock.sendall(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')

    def close(self):
        self.stream.close()
        self.sock.close()

    def rpc(self, method, params):
        self.sequence += 1
        request = {"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params}
        self.sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
        while True:
            line = self.stream.readline(8 * 1024 * 1024)
            if not line:
                raise RpcFailure("engine connection closed")
            response = json.loads(line)
            if response.get("id") == self.sequence:
                self.transcript.append({"request": request, "response": response})
                if response.get("error"):
                    raise RpcFailure(str(response["error"]))
                return response["result"]

    def tool(self, name, arguments):
        result = self.rpc("tools/call", {"name": name, "arguments": arguments})
        if result.get("isError"):
            raise RpcFailure(str(result))
        value = json.loads(
            "\n".join(c["text"] for c in result.get("content", []) if c.get("type") == "text")
        )
        if value.get("ok") is False or value.get("status") in ("error", "failed", "unsupported"):
            raise RpcFailure(str(value))
        return value

    def play(self, **request):
        return self.tool(
            "eve_play",
            {"request": {"schemaId": "evengine.play-request", "schemaVersion": 1, **request}},
        )

    def step(self, count=8):
        self.play(op="step", clock="frame", count=count)
        deadline = time.monotonic() + 10
        while not self.play(op="status").get("paused"):
            if time.monotonic() > deadline:
                raise RpcFailure("step did not finish")
            time.sleep(0.01)

    def observe(self):
        return self.play(op="observe", observation="combat-alive")["state"]


def check(identifier, actual, passed):
    return {"id": identifier, "passed": bool(passed), "actual": actual}


def contract_grade(project, original):
    try:
        candidate = read_json(project / "game.agent.json")
    except (OSError, ValueError) as exc:
        return [check("contract-valid-json", str(exc), False)]
    return [check("contract-exact-repair", candidate, candidate == original)]


def runtime_checks(client, category, seed):
    checks = []
    args = {"seed": seed, "enemyHp": 80, "playerHp": 100}
    if category == "restart":
        args.update(enemyHp=57 + seed % 11, playerHp=73 + seed % 13)
    client.tool("eve_restart", {"reloadScripts": False, "args": args})
    client.play(op="clock", mode="pause")
    client.play(op="act", action="reset")
    start = client.observe()
    checks.append(
        check(
            "initial-health",
            start,
            start["enemy.hp"] == args["enemyHp"] and start["player.hp"] == args["playerHp"],
        )
    )
    checks.append(
        check(
            "seed-and-max-health",
            start,
            start["seed"] == seed
            and start["enemy.maxHp"] == args["enemyHp"]
            and start["player.maxHp"] == args["playerHp"],
        )
    )
    # Stop on observed simulation time, not host frame duration. The host does not
    # promise a fixed dt. Raw observations preserve the actual dt for inspection.
    state = start
    elapsed = 0.0
    previous = start
    damage_consistent = True
    first_hit_time = None
    for _ in range(512):
        client.step()
        state = client.observe()
        elapsed = state["time"] - start["time"]
        hits = state["hits"] - previous["hits"]
        expected_hp = max(0, previous["enemy.hp"] - hits * start["player.attack"])
        damage_consistent = (
            damage_consistent and hits >= 0 and abs(state["enemy.hp"] - expected_hp) <= 0.0001
        )
        if hits and first_hit_time is None:
            first_hit_time = elapsed
        previous = state
        if state["enemy.hp"] <= 0 or state["player.hp"] <= 0 or elapsed >= 6.0:
            break
    checks.append(check("clock-progress", elapsed, 0 < elapsed <= 7.0))
    checks.append(check("damage-per-hit", state, damage_consistent))
    checks.append(
        check(
            "first-attack-timing",
            first_hit_time,
            first_hit_time is not None and 0.7 <= first_hit_time <= 1.2,
        )
    )
    checks.append(
        check(
            "attack-parameters",
            start,
            start["enemy.attack"] == 9.0
            and (
                16 <= start["player.attack"] <= 80 / 3
                if category == "balance"
                else start["player.attack"] == 14.0
            ),
        )
    )
    checks.append(
        check(
            "combat-not-bypassed",
            state,
            state["hits"] > start["hits"]
            and state["tick"] > start["tick"]
            and 0 < state["player.hp"] <= state["player.maxHp"]
            and state["enemy.hp"] >= 0,
        )
    )
    if category == "balance":
        checks.append(
            check("victory-window", elapsed, state["enemy.hp"] == 0 and 3.0 <= elapsed <= 4.5)
        )
    else:
        checks.append(
            check("enemy-damaged", state["enemy.hp"], state["enemy.hp"] < start["enemy.hp"])
        )
    snapshot = client.play(op="checkpoint", mode="capture")["json"]
    before = client.observe()
    client.step(8)
    client.play(op="checkpoint", mode="restore", json=snapshot)
    restored = client.observe()
    checks.append(check("checkpoint-exact-restore", restored, restored == before))
    client.play(op="act", action="reset")
    reset = client.observe()
    checks.append(
        check(
            "reset-after-combat",
            reset,
            reset["enemy.hp"] == args["enemyHp"]
            and reset["player.hp"] == args["playerHp"]
            and reset["hits"] == 0,
        )
    )
    return checks


def engine_grade(project, category, seed, engine, artifacts):
    # Grader instrumentation is installed only in the frozen evaluation copy.
    contract = read_json(project / "game.agent.json")
    contract["observations"][0]["fields"] = [
        "tick",
        "time",
        "hits",
        "seed",
        "player.hp",
        "player.maxHp",
        "enemy.hp",
        "enemy.maxHp",
        "player.attack",
        "enemy.attack",
    ]
    write_json(project / "game.agent.json", contract)
    with socket.socket() as port_socket:
        port_socket.bind(("127.0.0.1", 0))
        port = port_socket.getsockname()[1]
    transcript = []
    client = None
    # Environment pins tool visibility, not a session profile inherited from a shell.
    env = {k: v for k, v in os.environ.items() if not k.startswith("EVE_MCP_")}
    env["EVE_MCP_TOOLSET"] = "all"
    with (artifacts / "engine.log").open("wb") as log:
        process = subprocess.Popen(
            [str(engine), "run", "--debug", f"--mcp-port={port}", str(project)],
            cwd=project,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=False,
        )
        try:
            deadline = time.monotonic() + 30
            while client is None:
                if process.poll() is not None:
                    return [
                        check(
                            "engine-loads-candidate",
                            {"exitCode": process.returncode, "log": "engine.log"},
                            False,
                        )
                    ]
                try:
                    client = Client(port, transcript)
                except (ConnectionError, OSError):
                    if time.monotonic() > deadline:
                        raise RpcFailure("engine startup timeout")
                    time.sleep(0.05)
            client.play(op="clock", mode="pause")
            try:
                return runtime_checks(client, category, seed)
            except (RpcFailure, KeyError, ValueError) as exc:
                return [check("runtime-contract", str(exc), False)]
        finally:
            if client is not None:
                client.close()
            stop_process(process, grouped=False)
            write_json(artifacts / "grader-rpc.json", transcript)
