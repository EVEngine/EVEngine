"""Run the exact CI SDK with a bounded, separately compiled diagnostic plugin."""
from pathlib import Path
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts"))
import utf8_stdio


def main():
    executable = Path(sys.argv[1]).resolve()
    game = Path(__file__).resolve().parent / "sdk-game"
    log = Path("engine-probe.log").resolve()
    sample = Path("engine-probe-sample.txt").resolve()
    env = dict(os.environ, ALSOFT_DRIVERS="null", MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS="0")
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen([str(executable), "run"], cwd=game, env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        result = None
        while time.monotonic() < deadline:
            text = log.read_text(encoding="utf-8", errors="replace")
            marker = re.search(r"ENGINE_PROBE_DONE failures=(\d+)", text)
            if marker:
                result = int(marker.group(1))
                break
            if process.poll() is not None:
                break
            time.sleep(.1)
        if result is None and process.poll() is None:
            try:
                sampled = subprocess.run(["sample", str(process.pid), "1", "1", "-file", str(sample)],
                                         capture_output=True, encoding="utf-8", errors="replace", timeout=8)
                print(f"sample exit={sampled.returncode}: {sampled.stderr}")
            except (OSError, subprocess.TimeoutExpired) as error:
                print(f"sample unavailable: {error}")
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    print(log.read_text(encoding="utf-8", errors="replace"))
    print(f"SDK probe observed failures={result}, process exit={process.returncode}")
    return 0 if result == 0 else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
