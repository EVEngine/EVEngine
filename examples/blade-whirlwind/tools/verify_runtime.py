"""Verify a running blade-whirlwind example on MCP port 7547; outputs engine captures."""

import json
import socket
import time
from pathlib import Path
from PIL import Image, ImageChops

OUT = Path(__file__).resolve().parents[3] / "output" / "blade-whirlwind"


def call(name, args=None):
    with socket.create_connection(("127.0.0.1", 7547), timeout=40) as s:
        s.sendall(
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
        r = json.loads(s.makefile("rb").readline())
        if "error" in r or r.get("result", {}).get("isError"):
            raise RuntimeError(r)
        text = r["result"]["content"][0]["text"]
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text


def click(widget):
    call("eve_ui_click", {"host": "gale", "widget": widget})
    time.sleep(0.12)


def get(widget):
    return call("eve_ui_get", {"host": "gale", "widget": widget})


def paused():
    return get("phase")["text"].startswith("PAUSED")


def pause(value):
    if paused() != value:
        click("pause")


def set_parameters(values):
    # Test-only UI model instrumentation; attacks still use normal semantic UI events.
    code = 'ui.select("gale");' + "".join(
        f'ui.setValue("{key}",{float(value)});' for key, value in values.items()
    )
    call("eve_run_script", {"source": code})
    time.sleep(0.12)
    for key, value in values.items():
        assert abs(get(key)["value"] - value) < 0.001, (key, get(key), value)


def capture(name):
    for _ in range(5):
        result = call("eve_screenshot", {"path": "../../output/blade-whirlwind/" + name + ".png"})
        if result.get("ok"):
            return
        assert result.get("retryable"), result
        time.sleep(0.2)
    raise RuntimeError(result)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if get("auto")["text"].endswith("ON"):
        click("auto")
    pause(False)
    click("clear")
    click("jade")
    time.sleep(0.60)
    pause(True)
    capture("jade")
    a = get("phase")["text"]
    time.sleep(0.4)
    assert get("phase")["text"] == a
    click("depth")
    capture("depth-occluded")
    click("depth")
    base = Image.open(OUT / "jade.png").convert("RGB")
    depth = Image.open(OUT / "depth-occluded.png").convert("RGB")
    assert ImageChops.difference(base, depth).crop((350, 200, 1100, 750)).getbbox()
    click("clear")
    click("amber")
    pause(False)
    time.sleep(0.55)
    pause(True)
    capture("amber")
    assert get("status")["text"].startswith("amber-ink")
    click("clear")
    click("jade")
    click("clear")
    click("dual")
    pause(False)
    time.sleep(0.6)
    pause(True)
    capture("dual")
    click("clear")
    click("forward")
    pause(False)
    time.sleep(0.45)
    pause(True)
    capture("forward")
    assert abs(get("tilt")["value"] - 1.3) < 0.001
    # Exercise live size/speed and the next-trigger origin, direction and duration inputs.
    set_parameters(
        {
            "height": 4.2,
            "radius": 1.25,
            "spin": 12.0,
            "travel": 0.5,
            "x": 1.1,
            "y": 1.0,
            "z": -0.5,
            "tilt": 0.6,
            "heading": -0.4,
            "duration": 1.5,
        }
    )
    click("clear")
    click("attack")
    pause(False)
    time.sleep(0.5)
    pause(True)
    capture("custom-parameters")
    set_parameters(
        {
            "x": 0,
            "y": 0.12,
            "z": 0,
            "height": 3.5,
            "radius": 1.6,
            "spin": 8.5,
            "travel": 0,
            "tilt": 0,
            "heading": 0,
            "duration": 2.3,
        }
    )
    click("clear")
    click("attack")
    pause(False)
    frames = []
    times = []
    for i in range(26):
        capture(f"motion-{i:02d}")
        times.append(time.monotonic())
        frames.append(Image.open(OUT / f"motion-{i:02d}.png").convert("RGB"))
        time.sleep(0.065)
    assert ImageChops.difference(frames[0], frames[10]).getbbox()
    durations = [
        max(40, min(500, round((times[i + 1] - times[i]) * 1000))) for i in range(len(times) - 1)
    ] + [120]
    frames[0].save(
        OUT / "blade-whirlwind.gif",
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
    )
    click("cancel")
    time.sleep(1.2)
    assert "finished" in get("phase")["text"], get("phase")
    log = json.dumps(call("eve_console_read", {"limit": 1000}))
    assert "WHIRLWIND_CONTRACTS_PASS" in log and "BLADE_WHIRLWIND_READY" in log
    assert "failed:" not in log and "frame error:" not in log
    report = {
        "schema": "eve.example.whirlwind.validation",
        "version": 1,
        "engine": "running engine on localhost:7547",
        "renderPath": "Forward+ / Vulkan",
        "contractChecks": [
            "trigger",
            "busy",
            "duplicate",
            "freeze",
            "poseCopy",
            "isolation",
            "invalidPose",
            "negativeDt",
            "fade",
            "expiry",
            "dtPartition",
            "liveSpeed",
            "reuse",
            "schema",
            "close",
        ],
        "uiChecks": [
            "styleJade",
            "styleAmber",
            "dualCast",
            "forwardCast",
            "pause",
            "depthOccluder",
            "cancel",
            "customParameters",
        ],
        "parameterInstrumentation": "native UI model setValue through diagnostic tool; triggers through retained UI click events",
        "animationFrames": len(frames),
        "captures": ["jade", "amber", "dual", "forward", "depth-occluded", "custom-parameters"],
        "captureBackend": "engine MCP eve_screenshot",
    }
    (OUT / "validation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    # Leave a useful live preview in the visible window.
    set_parameters({"duration": 1.0})
    click("clear")
    click("attack")
    click("auto")


if __name__ == "__main__":
    main()
