"""Capture real Eve frames and verify independent silhouette/AO/shadow changes."""
import argparse
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def capture(eve, folder, output, port):
    env = os.environ.copy()
    env["PATH"] = str(eve.parent) + os.pathsep + str(ROOT / "build/win32-debug") + os.pathsep + env.get("PATH", "")
    with output.with_suffix(".log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(eve), "run", "--debug", f"--mcp-port={port}", str(folder)],
                                   env=env, stdout=log, stderr=log)
        stream = None
        connection = None
        try:
            for _ in range(80):
                if process.poll() is not None:
                    raise RuntimeError(f"Engine exited {process.returncode}; inspect {output.with_suffix('.log')}")
                try:
                    connection = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                    break
                except OSError:
                    time.sleep(0.15)
            if connection is None:
                raise RuntimeError("Engine MCP did not open")
            connection.settimeout(20)
            stream = connection.makefile("rwb")
            request_id = 0

            def rpc(method, params):
                nonlocal request_id
                request_id += 1
                stream.write((json.dumps(dict(jsonrpc="2.0", id=request_id, method=method, params=params)) + "\n").encode())
                stream.flush()
                while True:
                    line = stream.readline()
                    if not line:
                        raise RuntimeError("Engine MCP closed unexpectedly")
                    result = json.loads(line)
                    if result.get("id") == request_id:
                        if "error" in result:
                            raise RuntimeError(str(result["error"]))
                        return result["result"]

            rpc("initialize", dict(protocolVersion="2024-11-05", capabilities={},
                                   clientInfo=dict(name="impasto-verification", version="2")))
            time.sleep(2)
            for _ in range(20):
                result = rpc("tools/call", dict(name="eve_screenshot_image", arguments={}))
                for item in result.get("content", []):
                    if item["type"] == "image":
                        output.write_bytes(base64.b64decode(item["data"]))
                        return
                    if item["type"] == "text" and result.get("isError"):
                        raise RuntimeError(item["text"])
                time.sleep(0.15)
            raise RuntimeError("No presented engine frame")
        finally:
            if stream:
                stream.close()
            if connection:
                connection.close()
            process.terminate()
            process.wait(timeout=10)


def cube_interior_changes(enabled, disabled):
    """Independent projection oracle: erosion must not change the cube's face interior."""
    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    def cross(a, b):
        return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

    def normalized(v):
        length = math.sqrt(dot(v, v))
        return tuple(x / length for x in v)

    eye = (-0.55 + math.sin(0.12)*9.8, 3.8, math.cos(0.12)*9.8)
    target = (-0.55, 0.95, 0.0)
    forward = normalized(tuple(target[i] - eye[i] for i in range(3)))
    right = normalized(cross(forward, (0, 1, 0)))
    up = cross(right, forward)
    with Image.open(enabled) as image:
        on = image.convert("RGB")
    with Image.open(disabled) as image:
        off = image.convert("RGB")
    width, height = on.size
    tangent = math.tan(math.radians(38.0) / 2)
    points = []
    for x in (-3.8, -1.8):
        for y in (0.0, 2.0):
            for z in (-1.0, 1.0):
                relative = (x-eye[0], y-eye[1], z-eye[2])
                depth = dot(relative, forward)
                points.append((width/2*(1+dot(relative, right)/(depth*tangent*width/height)),
                               height/2*(1-dot(relative, up)/(depth*tangent))))

    def turn(a, b, c):
        return (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])

    hull = []
    ordered = sorted(points)
    for sequence in (ordered, list(reversed(ordered))):
        chain = []
        for point in sequence:
            while len(chain) >= 2 and turn(chain[-2], chain[-1], point) <= 0:
                chain.pop()
            chain.append(point)
        hull.extend(chain[:-1])
    mask = Image.new("L", on.size)
    ImageDraw.Draw(mask).polygon(hull, fill=255)
    # Exclude a six-pixel margin, larger than the maximum three-pixel erosion band.
    mask = mask.filter(ImageFilter.MinFilter(13))
    data = ImageChops.difference(on, off).tobytes()
    changed = sum(bool(value) and max(data[i*3:i*3+3]) > 8 for i, value in enumerate(mask.tobytes()))
    return changed, sum(bool(value) for value in mask.tobytes())


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--eve", type=Path, default=ROOT / "build/win32-debug/src/engine/eve.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "build/impasto-verification")
    parser.add_argument("--port", type=int, default=7546)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    modes = {"final": "", "no-edges": "impastoEdges=false",
             "no-solid-erosion": "impastoSolidErosion=false", "no-ao": "impastoAO=false",
             "no-sdf": "impastoAO=false impastoShadow=false", "orbit": "impastoYaw=0.40"}
    measurements = {}
    for index, (name, config) in enumerate(modes.items()):
        folder = output / ("scene-" + name)
        folder.mkdir(exist_ok=True)
        for file in ("main.nut", "parameters.nut"):
            shutil.copy2(HERE / file, folder / file)
        shutil.copytree(HERE / "assets", folder / "assets", dirs_exist_ok=True)
        (folder / "config.nut").write_text("config={width=1280 height=800 hotReload=false " + config + "};\n", encoding="utf-8")
        capture(args.eve.resolve(), folder, output / (name + ".png"), args.port + index)
        print("Captured", name, flush=True)
    with Image.open(output / "final.png") as image:
        baseline = image.convert("RGB")
    for name in modes:
        if name == "final":
            continue
        with Image.open(output / (name + ".png")) as image:
            diff = ImageChops.difference(baseline, image.convert("RGB"))
        data = diff.tobytes()
        changed = sum(max(data[index:index + 3]) > 8 for index in range(0, len(data), 3))
        if changed < 300:
            raise RuntimeError(f"{name}: only {changed} changed pixels; switch did not prove its effect")
        measurements[name] = changed
    interior_changes, interior_pixels = cube_interior_changes(output / "final.png", output / "no-solid-erosion.png")
    if interior_changes:
        raise RuntimeError(f"Cube erosion changed {interior_changes} interior pixels: silhouette cut escaped its border")
    print("PASS cube interior preserved:", interior_pixels, "pixels")
    sources = [HERE / "main.nut", HERE / "assets/brush-packed.png", HERE / "assets/impasto.vert.spv",
               HERE / "assets/impasto.frag.spv"]
    report = {"schema": "eve.impasto.verification", "version": 1, "threshold": "max RGB delta > 8/255",
              "changed_pixels": measurements, "cube_interior": {"tested_pixels": interior_pixels, "changed_pixels": interior_changes},
              "sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS: independent silhouette, AO, SDF shadow, and orbit changes", measurements)


if __name__ == "__main__":
    main()
