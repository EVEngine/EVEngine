#!/usr/bin/env python3
"""Verify a live companion-tactics instance via its declared Play Host contract."""

import argparse
import json
from pathlib import Path
import socket
import sys
import time


class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=180)
        self.stream = self.socket.makefile("rb")
        self.seq = 0
        self.call("initialize", {"protocolVersion": "2025-06-18", "clientInfo": {
            "name": "companion-verification", "version": "1.0"}})

    def call(self, method, params):
        self.seq += 1
        request = {"jsonrpc": "2.0", "id": self.seq, "method": method, "params": params}
        self.socket.sendall((json.dumps(request) + "\n").encode("utf-8"))
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("Engine closed the MCP connection")
            reply = json.loads(line)
            if reply.get("id") == self.seq:
                if "error" in reply:
                    raise RuntimeError(reply["error"])
                return reply["result"]

    def tool(self, name, arguments):
        result = self.call("tools/call", {"name": name, "arguments": arguments})
        text = "\n".join(item.get("text", "") for item in result.get("content", [])
                         if item.get("type") == "text")
        if result.get("isError") or text.startswith("error:"):
            raise RuntimeError(text)
        return json.loads(text)

    def play(self, **request):
        return self.tool("eve_play", {"request": {
            "schemaId": "evengine.play-request", "schemaVersion": 1, **request}})

    def close(self):
        self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7543)
    parser.add_argument("--output", type=Path, default=Path("output/companion-tactics"))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    frames = out / "frames"
    frames.mkdir(exist_ok=True)
    client = Client(args.port)
    try:
        status = client.play(op="status")
        assert status["contractId"] == "examples/companion-tactics", status
        client.play(op="act", action="verify")
        client.play(op="clock", mode="pause")
        for action in ("balanced", "focus", "protect", "retreat"):
            client.play(op="act", action=action)
            client.play(op="step", clock="frame", count=12)
            state = client.play(op="observe", observation="squad")["state"]
            assert state["command"] == action.upper(), state
        client.play(op="act", action="reset")
        # Engine readback is enabled by the first capture. Warm up one presented frame.
        try:
            client.play(op="capture", path=str(out / "overview.png"))
        except RuntimeError as exc:
            if "no presented frame" not in str(exc):
                raise
        observations = []
        for index in range(100):
            client.play(op="step", clock="frame", count=6)
            # Allow the GPU readback to become the most recently presented frame.
            time.sleep(0.025)
            client.play(op="capture", path=str(frames / f"{index:03}.png"))
            observations.append(client.play(op="observe", observation="squad")["state"])
        assert observations[-1]["tick"] > observations[0]["tick"]
        assert observations[-1]["tests"] == "passed"
        for key in ("heals", "guards", "dodges", "absorbed", "hits", "transitions"):
            assert observations[-1][key] > 0, (key, observations[-1])
        client.play(op="capture", path=str(out / "overview.png"))
        (out / "observations.json").write_text(
            json.dumps(observations, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        try:
            from PIL import Image
        except ImportError:
            print("PNG sequence captured; install Pillow to also create GIF.")
        else:
            images = []
            for index in range(100):
                with Image.open(frames / f"{index:03}.png") as image:
                    images.append(image.resize((1020, 630)).convert("P", palette=Image.Palette.ADAPTIVE))
            images[0].save(out / "companion-tactics.gif", save_all=True,
                           append_images=images[1:], duration=100, loop=0, disposal=2)
        print("PASS: native behavior tests, four commands, live outcomes and engine readback.")
        print(json.dumps(observations[-1], ensure_ascii=False))
        print(out)
    finally:
        try:
            client.play(op="clock", mode="play")
        finally:
            client.close()


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    main()
