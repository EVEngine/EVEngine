"""Bound native diagnostic cases and sample a pending process before termination."""
from pathlib import Path
import os
import re
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts"))
import utf8_stdio


def main():
    args = [os.environ["UNIT_TEST_BINARY"], *sys.argv[1:]]
    if "--list-test-cases" in args:
        return subprocess.run(args, check=False).returncode
    root = Path("compute-diagnostics")
    root.mkdir(exist_ok=True)
    name = re.sub(r"[^a-zA-Z0-9_.-]", "_", "_".join(sys.argv[1:]))[-150:]
    log = root / f"{name}-{time.time_ns()}.log"
    sample = log.with_suffix(".sample.txt")
    print(f"CASE_LAUNCH {args[1:]} log={log}", flush=True)
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
        try:
            result = process.wait(timeout=float(os.environ.get("WATCH_PENDING_SECONDS", "45")))
        except subprocess.TimeoutExpired:
            print(f"CASE_PENDING pid={process.pid}; sampling", flush=True)
            try:
                probe = subprocess.run(["sample", str(process.pid), "2", "1", "-file", str(sample)],
                                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
                print(f"sample result={probe.returncode}: {probe.stderr}")
            except subprocess.TimeoutExpired:
                print("sample timed out", flush=True)
            try:
                result = process.wait(timeout=float(os.environ.get("WATCH_GRACE_SECONDS", "15")))
            except subprocess.TimeoutExpired:
                process.kill()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    print("process did not retire after kill", flush=True)
                result = 124
    print(log.read_text(encoding="utf-8", errors="replace"), flush=True)
    if sample.exists():
        print(sample.read_text(encoding="utf-8", errors="replace"), flush=True)
    print(f"CASE_RESULT {result}", flush=True)
    return result


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
