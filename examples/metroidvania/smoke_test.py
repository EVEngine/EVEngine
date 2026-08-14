"""Launch Momentum Ruins and fail if the engine reports a frame/runtime error."""

from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
import time
from pathlib import Path


def replay_input(process_id: int) -> None:
    """Exercise movement, jumping, facing changes, combo attacks, and kicking."""
    if sys.platform != "win32":
        raise RuntimeError("--input-replay currently requires Windows")

    user32 = ctypes.windll.user32
    target = ctypes.c_void_p()
    enum_proc_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    @enum_proc_type
    def find_window(hwnd: int, _param: int) -> bool:
        owner_pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner_pid))
        if owner_pid.value == process_id and user32.IsWindowVisible(hwnd):
            target.value = hwnd
            return False
        return True

    deadline = time.monotonic() + 3.0
    while not target.value and time.monotonic() < deadline:
        user32.EnumWindows(find_window, 0)
        time.sleep(0.05)
    if not target.value:
        raise RuntimeError("game window did not appear")

    user32.SetForegroundWindow(target.value)

    def key(vk: int, held: float = 0.08) -> None:
        user32.keybd_event(vk, 0, 0, 0)
        time.sleep(held)
        user32.keybd_event(vk, 0, 2, 0)

    user32.keybd_event(ord("D"), 0, 0, 0)
    time.sleep(0.8)
    key(0x20, 0.12)  # jump while moving right
    time.sleep(0.65)
    user32.keybd_event(ord("D"), 0, 2, 0)
    for _ in range(3):
        key(ord("J"))
        time.sleep(0.2)
    key(ord("K"))
    user32.keybd_event(ord("A"), 0, 0, 0)
    time.sleep(0.35)
    key(0x20, 0.09)  # reverse-facing jump
    user32.keybd_event(ord("A"), 0, 2, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path, help="Path to the eve executable")
    parser.add_argument("--seconds", type=float, default=6.0)
    parser.add_argument("--input-replay", action="store_true")
    args = parser.parse_args()

    game_dir = Path(__file__).resolve().parent
    process = subprocess.Popen(
        [str(args.engine.resolve()), "run", str(game_dir)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    started = time.monotonic()
    replay_error = None
    if args.input_replay:
        try:
            replay_input(process.pid)
        except RuntimeError as exc:
            replay_error = str(exc)
    time.sleep(max(0.0, args.seconds - (time.monotonic() - started)))
    exited_early = process.poll() is not None
    if not exited_early:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
    stdout, stderr = process.communicate()
    combined = stdout + "\n" + stderr
    bad_markers = ("frame error", "Run failed:", "Runtime error")
    failures = [marker for marker in bad_markers if marker.lower() in combined.lower()]

    if failures or replay_error or (exited_early and process.returncode != 0):
        print(combined, file=sys.stderr)
        if replay_error:
            print("Input replay failed: " + replay_error, file=sys.stderr)
        print("Smoke test failed: " + ", ".join(failures), file=sys.stderr)
        return 1
    mode = " with input replay" if args.input_replay else ""
    print(f"Smoke test passed ({args.seconds:.1f}s{mode}, no frame/runtime errors).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
