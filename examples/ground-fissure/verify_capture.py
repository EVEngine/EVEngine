"""Verify a live example and capture actual engine frames. No eval or desktop capture.

Launch eve with --debug --mcp-port=7549, then run this from any directory.
Pillow is only required for image comparison and GIF encoding.
"""
import argparse
import json
from pathlib import Path
import socket
import time

from PIL import Image, ImageChops, ImageStat


class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=60)
        self.reader = self.socket.makefile("r", encoding="utf-8")
        self.sequence = 0

    def rpc(self, method, params):
        self.sequence += 1
        message = {"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params}
        self.socket.sendall((json.dumps(message) + "\n").encode("utf-8"))
        while True:
            line = self.reader.readline()
            if not line:
                raise RuntimeError("Engine MCP disconnected")
            response = json.loads(line)
            if response.get("id") == self.sequence:
                if "error" in response:
                    raise RuntimeError(str(response["error"]))
                return response["result"]

    def tool(self, name, arguments=None):
        result = self.rpc("tools/call", {"name": name, "arguments": arguments or {}})
        text = "\n".join(p["text"] for p in result.get("content", []) if p.get("type") == "text")
        if text.startswith("error:") or result.get("isError"):
            raise RuntimeError(text)
        try:
            return json.loads(text)
        except ValueError:
            return text

    def play(self, **request):
        request.update(schemaId="evengine.play-request", schemaVersion=1)
        return self.tool("eve_play", {"request": request})

    def state(self):
        return self.play(op="observe", observation="fault")["state"]

    def wait(self, predicate):
        end = time.monotonic() + 45
        while time.monotonic() < end:
            state = self.state()
            if predicate(state):
                return state
            time.sleep(0.03)
        raise AssertionError(f"State did not converge: {state}")

    def action(self, action, predicate):
        self.play(op="act", action=action)
        return self.wait(predicate)

    def capture(self, path):
        # State observation crosses a normal update boundary before readback.
        self.state()
        for _ in range(8):
            result = self.tool("eve_screenshot", {"path": str(path.resolve())})
            if result.get("ok"):
                assert result["width"] == 1280 and result["height"] == 800, result
                return result
            if not result.get("retryable"):
                raise RuntimeError(str(result))
            time.sleep(0.08)
        raise RuntimeError("No presented frame available")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7549)
    parser.add_argument("--output", type=Path, default=Path("output/ground-fissure/evidence"))
    parser.add_argument("--frames", type=int, default=96)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    frames_dir = args.output / "frames"
    frames_dir.mkdir(exist_ok=True)
    client = Client(args.port)
    status = client.play(op="status")
    assert status["contractId"] == "examples/ground-fissure", status
    session = client.tool("eve_agent_session_start", {
        "objective": "Validate parametric ground fissure, replay and engine-rendered output",
        "criteria": [
            {"id": "controls", "description": "Three presets, width changes and paused time stepping work"},
            {"id": "replay", "description": "Same seed and time reproduce scene pixels"},
            {"id": "visual", "description": "Actual engine PNG and evolving GIF frames are captured"},
        ],
    })
    session_id = session["sessionId"]
    for phase in ("modify", "run", "observe"):
        client.tool("eve_agent_session_advance", {"sessionId": session_id, "phase": phase})
    report = {"status": status, "presets": {}, "captures": [], "backend": "engine-readback"}
    for action, expected in (("earth", "DRY FAULT"), ("radial", "RADIAL SHATTER"), ("lava", "LAVA RIFT")):
        client.action(action, lambda s: s["preset"] == expected)
        state = client.action("opened", lambda s: s["paused"] and s["time"] > 3)
        path = args.output / f"{action}.png"
        client.capture(path)
        report["presets"][action] = state
        report["captures"].append(str(path.resolve()))
        print(f"PRESET_PASS {expected}", flush=True)

    width = client.state()["width"]
    narrow = client.action("narrower", lambda s: s["width"] < width - 0.2)
    wide = client.action("wider", lambda s: abs(s["width"] - width) < 0.001)
    report["widthCheck"] = {"before": width, "narrow": narrow["width"], "restored": wide["width"]}
    # Exercise the visible Replay button, not just the script-map transport.
    count = client.state()["replays"]
    client.tool("eve_ui_click", {"host": "fault", "widget": "replay"})
    client.wait(lambda s: s["replays"] > count)
    client.action("opened", lambda s: s["paused"] and abs(s["time"] - 4.4) < 0.001)
    replay_path = args.output / "replay.png"
    client.capture(replay_path)
    with Image.open(args.output / "lava.png") as original, Image.open(replay_path) as replay:
        crop = (325, 100, 1270, 760)
        difference = ImageStat.Stat(ImageChops.difference(original.convert("RGB").crop(crop), replay.convert("RGB").crop(crop))).mean
    assert max(difference) < 0.1, difference
    report["replayMeanPixelError"] = difference
    print(f"REPLAY_PASS meanPixelError={difference}", flush=True)

    client.action("intact", lambda s: s["paused"] and s["time"] < 0)
    client.capture(args.output / "intact.png")
    first_time = client.state()["time"]
    hashes = set()
    frame_paths = []
    for index in range(args.frames):
        target = first_time + (index + 1) * 0.0625
        state = client.action("step", lambda s: s["paused"] and abs(s["time"] - target) < 0.0001)
        path = frames_dir / f"{index:03d}.png"
        client.capture(path)
        frame_paths.append(path)
        with Image.open(path) as frame:
            hashes.add(frame.crop((325, 100, 1270, 760)).tobytes())
        if index % 16 == 0:
            print(f"CAPTURE {index + 1}/{args.frames} time={state['time']:.4f}", flush=True)
    assert len(hashes) > args.frames * 0.7, len(hashes)
    images = [Image.open(path).convert("RGB").resize((960, 600), Image.Resampling.LANCZOS) for path in frame_paths]
    palette = images[len(images) // 2].quantize(colors=256)
    indexed = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in images]
    durations = [60 if i % 4 != 1 else 70 for i in range(len(images))]
    durations[0] = 400
    durations[-1] = 800
    gif_path = args.output / "ground-fissure.gif"
    indexed[0].save(gif_path, save_all=True, append_images=indexed[1:], duration=durations, loop=0, optimize=False, disposal=1)
    report.update(frameCount=args.frames, uniqueSceneFrames=len(hashes), gif=str(gif_path.resolve()))
    client.action("opened", lambda s: s["paused"] and s["time"] > 4)
    for criterion, kind, artifact in (
        ("controls", "runtime-observation", ""),
        ("replay", "test", str(replay_path.resolve())),
        ("visual", "screenshot", str((args.output / "lava.png").resolve())),
    ):
        client.tool("eve_agent_session_evidence", {
            "sessionId": session_id, "criterionId": criterion, "kind": kind, "status": "pass",
            "summary": f"Verified {criterion} through native UI/Play Host; see verification.json", "artifact": artifact,
        })
    client.tool("eve_agent_session_advance", {"sessionId": session_id, "phase": "verify"})
    report["evidenceSession"] = client.tool("eve_agent_session_complete", {
        "sessionId": session_id, "summary": "Presets, parameter changes, replay and actual rendered animation verified",
    })
    errors = client.tool("eve_console_read", {"level": "error", "limit": 100})
    assert errors.get("count", 0) == 0, errors
    report["runtimeErrors"] = errors
    (args.output / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"GROUND_FISSURE_VERIFIED frames={args.frames} unique={len(hashes)} gif={gif_path}", flush=True)


if __name__ == "__main__":
    main()
