#!/usr/bin/env python3
"""Consumer acceptance for Apple SDK bundles, including signature preservation."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

import utf8_stdio


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host_sdk", type=Path)
    parser.add_argument("target_sdk", type=Path)
    parser.add_argument("--platform", choices=("macos", "ios"), default="macos")
    parser.add_argument("--simulator", default="", help="Booted simulator UDID")
    parser.add_argument("--unsigned", action="store_true")
    parser.add_argument("--device", default="")
    args = parser.parse_args()
    host, target = args.host_sdk.resolve(), args.target_sdk.resolve()
    with tempfile.TemporaryDirectory(prefix="eve-apple-consumer-") as temporary:
        root = Path(temporary)
        game = root / "game"
        game.mkdir()
        (game / "config.nut").write_text(
            'config = { width=320 height=240 title="apple-sdk-test" debug=false hotReload=false };\n',
            encoding="utf-8")
        (game / "main.nut").write_text(
            'eve_init = function() { print("EVE_CI_GAME_OK\\n"); };\neve_update = function(dt) {};\n',
            encoding="utf-8")
        command = [str(host / "bin/eve"), "build", args.platform, str(game), "--sdk", str(target),
                   "-o", str(root / "output"), "--name", "Consumer", "--bundle-id", os.environ.get("IOS_BUNDLE_ID", "com.evengine.consumer")
                   if args.platform == "ios" and not args.unsigned and not args.simulator
                   else "com.evengine.consumer"]
        if args.unsigned:
            command += ["--unsigned"]
        elif args.simulator:
            command += ["--simulator"]
        elif args.platform == "ios":
            command += ["--sign-identity", os.environ["IOS_SIGN_IDENTITY"],
                        "--profile", os.environ["IOS_PROFILE_PATH"]]
            if args.device:
                command += ["--device", args.device]
        subprocess.run(command, check=True)
        app = root / "output/Consumer.app"
        if args.unsigned:
            if not (app / "game/main.nut").is_file():
                raise RuntimeError("Unsigned assembly lost the consumer game")
            print("PASS: unsigned iOS consumer assembly (no signature or device run claimed)")
            return
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        if args.platform == "ios" and not args.simulator:
            if not (root / "output/Consumer.ipa").is_file():
                raise RuntimeError("Signed device assembly did not produce an IPA")
            if not args.device:
                print("PASS: development signing and IPA assembly; device runtime is a separate gate")
                return
        if args.device:
            launch = ["xcrun", "devicectl", "device", "process", "launch", "--console",
                      "--terminate-existing", "--device", args.device, os.environ["IOS_BUNDLE_ID"]]
        elif args.simulator:
            subprocess.run(["xcrun", "simctl", "install", args.simulator, str(app)], check=True)
            launch = ["xcrun", "simctl", "launch", "--console", "--terminate-running-process",
                      args.simulator, "com.evengine.consumer"]
        else:
            launch = [str(app / "Contents/MacOS/eve")]
        with (root / "runtime.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen(launch, cwd=root, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    text = (root / "runtime.log").read_text(encoding="utf-8", errors="replace")
                    if "EVE_CI_GAME_OK" in text and "first present" in text:
                        time.sleep(2)
                        if process.poll() is not None:
                            raise RuntimeError("Game exited immediately after startup")
                        print(text)
                        break
                    if process.poll() is not None:
                        raise RuntimeError(f"Game exited before startup marker:\n{text}")
                    time.sleep(0.25)
                else:
                    raise RuntimeError(f"Startup marker absent:\n{text}")
            finally:
                if args.simulator:
                    subprocess.run(["xcrun", "simctl", "terminate", args.simulator,
                                    "com.evengine.consumer"], check=True)
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        print("PASS: consumer app started, stayed alive and preserved its signature after running")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
