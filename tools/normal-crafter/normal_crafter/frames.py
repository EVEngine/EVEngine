"""Frame-sequence I/O helpers.

NormalCrafter's reference implementation consumes a *video* file.  For game
assets the inputs are usually *PNG sequence frames* (sprites / flipbook / VFX),
so this module bridges the two:

* a directory of PNG frames  -> a temporary mp4 (then cleaned up)
* the output normal video    -> a directory of normal PNG frames

`ffmpeg` (via `ffmpy`) is preferred because it handles arbitrary codecs and FPS
robustly; OpenCV is used as a fallback when ffmpeg is unavailable.
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
from typing import List, Optional, Tuple

import numpy as np

_FRAME_RE = re.compile(r".*?(\d+)\.png$", re.IGNORECASE)

VIDEO_EXTS = (".mp4", ".mov", ".avi", ".webm", ".mkv", ".m4v")


def is_video(path: str) -> bool:
    return os.path.splitext(path)[1].lower() in VIDEO_EXTS


def _ffmpeg_available() -> bool:
    try:
        subprocess.run(
            ["ffmpeg", "-version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        return True
    except FileNotFoundError:
        return False


def list_frame_paths(folder: str) -> List[str]:
    """Return PNG frame paths sorted by the numeric part of their filename."""
    entries = []
    for name in os.listdir(folder):
        m = _FRAME_RE.match(name)
        if m:
            entries.append((int(m.group(1)), name))
    entries.sort(key=lambda t: t[0])
    return [os.path.join(folder, name) for _, name in entries]


def frames_to_video(frame_paths: List[str], fps: int, out_mp4: str) -> None:
    """Encode an ordered list of PNG frames into an mp4 at the given FPS."""
    if not frame_paths:
        raise ValueError("empty frame sequence")
    if _ffmpeg_available():
        import ffmpy  # type: ignore

        ffmpy.FFmpeg(
            inputs={frame_paths[0]: None},
            outputs={
                out_mp4: [
                    "-y",
                    "-framerate",
                    str(fps),
                    "-start_number",
                    _frame_number(frame_paths[0]),
                    "-i",
                    os.path.join(os.path.dirname(frame_paths[0]), "%06d.png"),
                    "-c:v",
                    "libx264",
                    "-pix_fmt",
                    "yuv420p",
                ]
            },
        ).run()
    else:
        import cv2

        frame = cv2.imread(frame_paths[0])
        h, w = frame.shape[:2]
        writer = cv2.VideoWriter(
            out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), float(fps), (w, h)
        )
        for p in frame_paths:
            writer.write(cv2.imread(p))
        writer.release()


def video_to_frames(video_path: str, out_folder: str, ext: str = "png") -> List[str]:
    """Decode every frame of a video into `<out_folder>/frame_%06d.<ext>`."""
    os.makedirs(out_folder, exist_ok=True)
    if _ffmpeg_available():
        import ffmpy  # type: ignore

        out_pattern = os.path.join(out_folder, "frame_%06d." + ext)
        ffmpy.FFmpeg(
            inputs={video_path: None},
            outputs={out_pattern: ["-y", "-vsync", "0"]},
        ).run()
    else:
        import cv2

        cap = cv2.VideoCapture(video_path)
        idx = 1
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            cv2.imwrite(os.path.join(out_folder, f"frame_{idx:06d}.{ext}"), frame)
            idx += 1
        cap.release()
    return list_frame_paths(out_folder)


def read_fps(video_path: str) -> int:
    """Read FPS of a video.  Falls back to a default on failure."""
    try:
        import cv2

        cap = cv2.VideoCapture(video_path)
        fps = cap.get(cv2.CAP_PROP_FPS)
        cap.release()
        if fps and fps > 0:
            return int(round(fps))
    except Exception:
        pass
    return 30


def _frame_number(path: str) -> str:
    m = _FRAME_RE.match(os.path.basename(path))
    return m.group(1) if m else "1"


def probe_sequence(folder: str) -> Tuple[int, int, Optional[Tuple[int, int]]]:
    """Return (frame_count, fps_guess, (h, w)) for a PNG frame folder."""
    paths = list_frame_paths(folder)
    if not paths:
        return 0, 30, None
    try:
        import cv2

        img = cv2.imread(paths[0])
        h, w = img.shape[:2]
    except Exception:
        h = w = None
    return len(paths), 30, (h, w) if h else None
