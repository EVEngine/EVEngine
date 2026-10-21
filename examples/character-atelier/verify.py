"""Launch the atelier, exercise its UI, and capture engine-owned visual evidence."""

import argparse
import ctypes
import json
import os
import re
from pathlib import Path
import socket
import subprocess
import time

from PIL import Image, ImageChops, ImageStat


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "verification")
    parser.add_argument("--initial-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]

    def call(name, arguments):
        with socket.create_connection(("127.0.0.1", port), timeout=10) as stream:
            stream.settimeout(60)
            stream.sendall((json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                        "params": {"name": name, "arguments": arguments}}) + "\n").encode())
            with stream.makefile("rb") as reader:
                for line in reader:
                    response = json.loads(line)
                    if response.get("id") != 1:
                        continue
                    if "error" in response:
                        raise RuntimeError(response["error"])
                    result = response["result"]
                    text = "\n".join(c.get("text", "") for c in result.get("content", []))
                    if result.get("isError") or text.startswith("error:"):
                        raise RuntimeError(text)
                    return text
        raise RuntimeError("MCP closed without a response")

    def check(source):
        call("eve_run_script", {"source": source})

    def click(widget, host="atelier"):
        result = call("eve_ui_click", {"host": host, "widget": widget})
        if '"ok":false' in result.replace(" ", ""):
            raise RuntimeError(result)
        time.sleep(1.0)

    def capture(name):
        path = output / (name + ".png")
        path.unlink(missing_ok=True)
        for _ in range(8):
            text = call("eve_screenshot", {"path": path.as_posix()})
            if path.exists():
                print("Captured", name, flush=True)
                return path
            if '"retryable":true' not in text.replace(" ", ""):
                raise RuntimeError(text)
            time.sleep(0.5)
        raise RuntimeError("No rendered screenshot")

    log_path = output / "runtime.log"
    with log_path.open("w", encoding="utf-8") as log:
        environment = os.environ.copy()
        environment["XDG_DATA_HOME"] = str(output / "save-data")
        process = subprocess.Popen([str(args.engine.resolve()), "run", "--mcp-port=" + str(port)],
                                   cwd=Path(__file__).parent, stdout=log, stderr=subprocess.STDOUT, env=environment)
        try:
            deadline = time.monotonic() + 90
            while "first present" not in log_path.read_text(encoding="utf-8"):
                if process.poll() is not None:
                    raise RuntimeError(log_path.read_text(encoding="utf-8"))
                if time.monotonic() > deadline:
                    raise RuntimeError("Engine did not present within 90 seconds")
                time.sleep(0.25)
            runtime = log_path.read_text(encoding="utf-8")
            if "eve_init failed" in runtime or "frame error:" in runtime or "CHARACTER_ATELIER_READY" not in runtime:
                raise RuntimeError(runtime)
            print("Engine presented", flush=True)
            check('if(atelierCharacter.parts.len()!=13 || atelierRigData.controls.len()!=15) throw "Rig import failed"; if(atelierCharacter.style.getStyle()!="ysa") throw "YSA not active";')
            click("point-eyeL")
            initial = capture("01-editor-face")
            if args.initial_only:
                return
            check('foreach(name in ["Tops_01_CLOTH","Bottoms_01_CLOTH","Shoes_01_CLOTH"]) {local found=false;foreach(n in atelierCharacter.materialNames) if(n==name) found=true;if(!found) throw "Missing original garment material";}')
            tree = json.loads(call("eve_ui_tree", {}))
            (output / "ui-tree.json").write_text(json.dumps(tree, indent=2), encoding="utf-8")
            # Use real injected mouse events to pick and drag the left eye landmark.
            check('mouse.setPosition(650.0,450.0);')
            call("eve_input_batch", {"actions": [{"op": "mouse_move", "x": 650, "y": 450}]})
            time.sleep(1)
            check('ui.select("atelier"); foreach(p in atelier.projected) if(p.id=="eyeL") print(format("TEST_POINT %.3f %.3f\\n",mouse.getX()-ui.viewportMouseX("stage")+p.screen.x,mouse.getY()-ui.viewportMouseY("stage")+p.screen.y));')
            time.sleep(0.5)
            match = re.findall(r"TEST_POINT ([\d.]+) ([\d.]+)", log_path.read_text(encoding="utf-8"))[-1]
            px, py = map(float, match)
            # Real X11 input updates SDL_GetMouseState as well as ImGui. SDL_PushEvent-only
            # injection does not hold a mouse button in SDL's polled state on this host.
            x11 = ctypes.CDLL("libX11.so.6")
            xtest = ctypes.CDLL("libXtst.so.6")
            x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
            x11.XOpenDisplay.restype = ctypes.c_void_p
            x11.XFlush.argtypes = [ctypes.c_void_p]
            xtest.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
            xtest.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
            display = x11.XOpenDisplay(None)
            if not display:
                raise RuntimeError("No X11 display for actual mouse interaction")
            def mouse(op, **values):
                if op == "mouse_move":
                    xtest.XTestFakeMotionEvent(display, -1, round(values["x"]), round(values["y"]), 0)
                else:
                    xtest.XTestFakeButtonEvent(display, values["button"], int(values["down"]), 0)
                x11.XFlush(display)
                time.sleep(0.6)
            mouse("mouse_move", x=px, y=py)
            mouse("mouse_button", button=1, down=True, x=px, y=py)
            mouse("mouse_move", x=px+14, y=py-9, dx=14, dy=-9)
            mouse("mouse_button", button=1, down=False, x=px+14, y=py-9)
            check('if(atelier.snapshot.parameters.face_eyeL_x<=0.0 || atelier.snapshot.parameters.face_eyeR_x>=0.0 || atelier.gesture!=null) throw "Mirrored mouse drag failed";')
            shaped = capture("02-mirrored-eye-shape")
            click("undo")
            check('if(atelier.snapshot.parameters.face_eyeL_x!=0.0 || atelier.snapshot.parameters.face_eyeR_x!=0.0) throw "Drag was not one undo transaction";')
            undone = capture("03-undo-face")
            click("redo")
            click("save")
            check('if(atelier.status!="Saved appearance.json") throw "Save failed";')
            click("reset")
            click("load")
            check('if(atelier.snapshot.parameters.face_eyeL_x<=0.0) throw "Bone appearance save/load failed";')
            check('local before=acChecked(atelier.rig.snapshotJson());local r=atelier.rig.apply({parameters={face_eyeL_x=0.1}});if(r.ok || acChecked(atelier.rig.snapshotJson())!=before) throw "Invalid edit was not atomic";')
            # Strong face change verifies weighted skin joints, not just moving handles.
            click("reset")
            click("points")
            no_points = capture("04-face-no-handles")
            check('acEdit({parameters={face_jawL_x=0.012,face_jawR_x=-0.012,face_chin_y=-0.012,face_cheekL_sx=1.18,face_cheekR_sx=1.18}});')
            jaw = capture("05-jaw-bone-deformation")
            click("mode-Body")
            neutral_body = capture("06-body-default")
            check('acEdit({parameters={height=1.1,shoulders=1.15,waist=0.85,hips=1.15,legs=1.12,arms=1.1,head=0.95}});')
            body = capture("07-body-proportions")
            check('local foot=arWorld(atelierRigData.humanoid.leftFoot);if(foot[1]<0.05 || foot[1]>0.10) throw "Foot grounding failed";')
            click("reset")
            click("mode-Pose")
            for name in ["idle", "weight", "hips", "wave", "think", "tpose"]:
                click("pose-"+name)
                check('if(atelier.snapshot.selections.pose!="'+name+'") throw "Pose selection failed";')
                if name == "hips":
                    check('local hand=arWorld(atelierRigData.humanoid.rightHand),hip=arWorld(atelierRigData.humanoid.hips);if(abs(hand[1]-hip[1])>0.08 || abs(hand[0]-hip[0])>0.20) throw "Hands are not at hips";')
                if name == "think":
                    check('local hand=arWorld(atelierRigData.humanoid.rightHand),chin=arWorld("Atelier_chin");if(abs(hand[1]-chin[1])>0.10 || abs(hand[0]-chin[0])>0.10) throw "Thinking hand missed chin";')
                capture("08-pose-"+name)
            click("pose-wave")
            click("play")
            check('if(!atelier.playing) throw "Pose playback did not start";')
            motion_a = capture("09-wave-motion-a")
            time.sleep(0.5)
            motion_b = capture("09-wave-motion-b")
            click("play")
            click("reset")
            click("mode-Expression")
            check('acEdit({parameters={happy=0.7,blink=0.4}});')
            capture("10-expression-on-rig")
            crop = (320, 150, 1180, 850)
            def difference(a, b):
                a = Image.open(a).convert("RGB").crop(crop)
                b = Image.open(b).convert("RGB").crop(crop)
                return sum(ImageStat.Stat(ImageChops.difference(a,b)).mean)
            metrics = {"eyeDrag": difference(initial, shaped), "undo": difference(initial, undone),
                       "faceSkin": difference(no_points, jaw), "bodySkin": difference(neutral_body, body),
                       "waveMotion": difference(motion_a, motion_b)}
            for key in ["eyeDrag", "faceSkin", "bodySkin", "waveMotion"]:
                if metrics[key] < 0.15:
                    raise RuntimeError("No visible deformation: "+key+" "+str(metrics))
            if metrics["undo"] > 0.15:
                raise RuntimeError("Undo did not restore original skin deformation")
            report = {"renderer": "ysa", "facialJoints": 15, "posePresets": 6,
                      "realMouseLandmarkDrag": True, "mirroredEdit": True, "singleDragUndo": True,
                      "saveLoad": True, "invalidInputAtomic": True, "metrics": metrics,
                      "screenshots": len(list(output.glob("*.png")))}
            (output / "report.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
            print(json.dumps(report), flush=True)
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
