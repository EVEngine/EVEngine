"""Generate a four-second knight greeting through existing Editor MCP commands.

Run eve run --mcp-port=7549 in this example, then python agent_sequence_demo.py.
Edits the live Rig as one undo step; bakes through the example UI and leaves it playing.
Writes a reusable Rig JSON and actual engine-rendered frames. Pillow enables GIF output.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import time

from agent_demo import call


TIMES = [0.0, 0.5, 1.0, 1.4, 1.8, 2.2, 2.6, 3.0, 4.0]
ANGLES = {
    "upperarm.l": [0, 45, 75, 55, 80, 55, 80, 45, 0],
    "upperarm.r": [0, 35, 50, 50, 50, 50, 50, 35, 0],
    "chest": [0, -4, -8, -4, -8, -4, -8, -4, 0],
    "head": [0, 3, 6, 3, 6, 3, 6, 3, 0],
}


def rotate_parent_z(initial, degrees):
    """Precompose a parent-local Z rotation with the imported bind quaternion."""
    transform = list(initial)
    x, y, z, w = transform[3:7]
    half = math.radians(degrees) * 0.5
    sine, cosine = math.sin(half), math.cos(half)
    rotation = [cosine*x-sine*y, cosine*y+sine*x,
                cosine*z+sine*w, cosine*w-sine*z]
    length = math.sqrt(sum(value*value for value in rotation))
    transform[3:7] = [value/length for value in rotation]
    return transform


def generate(document):
    candidate = copy.deepcopy(document)
    controls = {control["id"]: control for control in candidate["controls"]}
    missing = set(ANGLES) - controls.keys()
    if missing:
        raise ValueError(f"This demo needs the knight controls: {sorted(missing)}")
    candidate["duration"] = 4.0
    for control in candidate["controls"]:
        control["keys"] = []
    for name, angles in ANGLES.items():
        control = controls[name]
        control["keys"] = [{"time": seconds, "transform": rotate_parent_z(control["initial"], angle)}
                           for seconds, angle in zip(TIMES, angles)]
    return candidate


def run(port, output, frame_count):
    output.mkdir(parents=True, exist_ok=True)
    def tool(name, **arguments):
        return call(port, name, arguments)
    def click(widget):
        tool("eve_ui_click", host="Timeline", widget=widget)
        time.sleep(0.2)
    targets = tool("eve_editor_target_list")["targets"]
    target = next(item["id"] for item in targets if item.get("type") == "control-rig")
    command = next(item for item in tool("eve_editor_commands")["commands"]
                   if item["id"] == "animation.rig.replace.v1")
    assert "keys" in command["inputSchema"]["properties"]["controls"]["items"]["properties"]
    click("edit")
    before = tool("eve_editor_inspect", target=target)["target"]
    original = before["snapshot"]
    candidate = generate(original)
    plan = tool("eve_editor_plan", target=target, command=command["id"], payload=candidate,
                expectedRevision=before["descriptor"]["revision"])
    assert tool("eve_editor_inspect", target=target)["target"]["snapshot"] == original
    receipt = tool("eve_editor_commit", planId=plan["planId"])
    assert receipt["status"] == "applied", receipt
    authored = tool("eve_editor_inspect", target=target)["target"]["snapshot"]
    tracks = [control for control in authored["controls"] if control["keys"]]
    assert len(tracks) == 4 and sum(len(control["keys"]) for control in tracks) == 36
    for control in tracks:
        assert [key["time"] for key in control["keys"]] == TIMES
        assert control["keys"][0]["transform"] == control["keys"][-1]["transform"]
    # Prove the generated sequence is one reversible edit, not per-key mutation.
    tool("eve_editor_undo", target=target)
    assert tool("eve_editor_inspect", target=target)["target"]["snapshot"] == original
    tool("eve_editor_redo", target=target)
    assert tool("eve_editor_inspect", target=target)["target"]["snapshot"] == authored
    (output / "greeting.rig.json").write_text(json.dumps(authored, indent=2), encoding="utf-8")
    (output / "original.rig.json").write_text(json.dumps(original, indent=2), encoding="utf-8")
    click("bake")
    status = tool("eve_ui_get", host="Pose", widget="status")
    assert status.get("text") == "Playing baked native AnimClip", status
    paths, timestamps, hashes = [], [], []
    started = time.monotonic()
    for index in range(frame_count):
        path = output / f"sequence-{index:03d}.png"
        path.unlink(missing_ok=True)
        for attempt in range(8):
            try:
                tool("eve_screenshot", path=str(path.resolve()))
                if path.exists():
                    break
            except RuntimeError:
                if attempt == 7:
                    raise
            time.sleep(0.1)
        if not path.exists():
            raise RuntimeError(f"Engine screenshot was not produced: {path}")
        paths.append(path)
        timestamps.append(time.monotonic() - started)
        hashes.append(hashlib.sha256(path.read_bytes()).hexdigest())
        time.sleep(0.13)
    assert len(set(hashes)) > frame_count // 2, "Playback frames did not change"
    result = {"passed": True, "target": target, "durationSeconds": 4,
              "animatedControls": list(ANGLES), "tracks": 4, "keys": 36,
              "oneUndoStepVerified": True, "bakedPlaybackStatus": status["text"],
              "receipt": receipt, "frameSeconds": timestamps,
              "uniqueFrameHashes": len(set(hashes)),
              "rigDocument": "greeting.rig.json", "clipStorage": "runtime AnimClip (not exported as a clip file)"}
    try:
        from PIL import Image, ImageChops
    except ImportError:
        result["gif"] = "not produced: install Pillow; original engine PNGs remain available"
    else:
        frames = []
        changed_pixels = []
        previous = None
        for path in paths:
            with Image.open(path) as source:
                frame = source.convert("RGB")
            viewport = frame.crop((235, 95, 1060, 540))
            if previous is not None:
                pixels = ImageChops.difference(previous, viewport).get_flattened_data()
                changed_pixels.append(sum(pixel != (0, 0, 0) for pixel in pixels))
            previous = viewport
            frames.append(frame.resize((960, 600), Image.Resampling.LANCZOS))
        assert max(changed_pixels) > 1000, "No visible pose motion in engine viewport"
        durations = [max(20, round((b-a)*1000)) for a, b in zip(timestamps, timestamps[1:])]
        durations.append(durations[-1])
        frames[0].save(output / "greeting.gif", save_all=True, append_images=frames[1:],
                       duration=durations, loop=0, optimize=False)
        result["gif"] = "greeting.gif"
        result["maxChangedViewportPixels"] = max(changed_pixels)
    (output / "sequence-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7549)
    parser.add_argument("--output", type=Path, default=Path("evidence/sequence"))
    parser.add_argument("--frames", type=int, default=40)
    args = parser.parse_args()
    if not 8 <= args.frames <= 240:
        parser.error("--frames must be between 8 and 240")
    run(args.port, args.output, args.frames)
