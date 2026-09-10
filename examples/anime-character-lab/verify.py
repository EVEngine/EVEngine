"""Capture comparable engine frames and exercise shader replacement via local MCP."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--port", type=int, default=7537)
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "verification")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def call(name, arguments):
        with socket.create_connection(("127.0.0.1", args.port), timeout=10) as stream:
            stream.settimeout(30)
            request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": arguments}}
            stream.sendall((json.dumps(request) + "\n").encode())
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

    def script(source):
        call("eve_run_script", {"source": source})
        time.sleep(0.2)  # Let the modified scene present before its readback.

    def capture(name):
        path = output / (name + ".png")
        for attempt in range(10):
            try:
                call("eve_screenshot", {"path": path.as_posix()})
                print(path)
                return path
            except RuntimeError as error:
                if "no presented frame" not in str(error) or attempt == 9:
                    raise
                time.sleep(0.1)

    script('if(!lab.ready)throw "Wait for [anime-lab] ready before verification";')
    try:
        script('lab.yaw=0.947;setView(true);setLighting(0);setStyle("anime");')
        before = capture("anime-portrait")
        script('''
foreach(words in [["bad"],[1.5],[-1],[4294967296],[0xDEADBEEF]]) {
    local result=gfx.replaceShaderFromSpv(lab.anime[0],[],words);
    if(result.ok)throw "Invalid SPIR-V unexpectedly accepted";
}
''')
        after = capture("rejected-reload")
        if hashlib.sha256(before.read_bytes()).digest() != hashlib.sha256(after.read_bytes()).digest():
            raise RuntimeError("Rejected reload changed the rendered frame")
        script("reloadAnime();")
        reloaded = capture("valid-reload")
        if before.read_bytes() != reloaded.read_bytes():
            raise RuntimeError("Identical shader reload changed the rendered frame")
        script('setStyle("cartoon");')
        capture("legacy-portrait")
        script('setStyle("anime");setView(false);')
        capture("anime-full")
        script('setStyle("cartoon");')
        capture("legacy-full")
        for light, label in [(1, "side"), (2, "back")]:
            script(f'setStyle("anime");setView(true);setLighting({light});')
            capture("anime-" + label)
        script('setLighting(0);lab.yaw=-0.3;setView(true);')
        capture("anime-orbit")
        script('lab.yaw=0.947;setView(true);setLighting(0);gfx.getRenderControl().disable("shadow");')
        no_shadows = capture("anime-no-shadows")
        if before.read_bytes() == no_shadows.read_bytes():
            raise RuntimeError("Shadow toggle has no visible effect; check the shadow-casting light")
        print("PASS: invalid reload preserves pixels; valid reload preserves pixels; views captured")
    finally:
        script('gfx.getRenderControl().enable("shadow");lab.yaw=0.947;setView(true);setLighting(0);setStyle("anime");')


if __name__ == "__main__":
    main()
