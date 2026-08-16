"""Thin CPU client for the NormalCrafter GPU server.

Handles all frame <-> video bridging and PNG I/O on the artist's machine; the
heavy GPU inference stays on the server.  The client:

1. encodes a PNG frame directory into a temp mp4 (or reuses a video input),
2. POSTs it to `/v1/normal-crafter`,
3. unpacks the returned ZIP of normal-map PNGs into the output directory.
"""

from __future__ import annotations

import io
import os
import tempfile
import zipfile
from typing import List, Optional, Tuple

import requests

from .frames import frames_to_video, is_video, list_frame_paths


class NormalCrafterClient:
    def __init__(self, base_url: str, timeout: float = 1800.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def health(self) -> bool:
        try:
            r = requests.get(f"{self.base_url}/health", timeout=30)
            return r.status_code == 200 and r.json().get("status") == "ok"
        except requests.RequestException:
            return False

    def infer(
        self,
        video_path: str,
        max_res: int = 1024,
        target_fps: int = 0,
        window_size: int = 14,
        time_step_size: int = 10,
        decode_chunk_size: int = 7,
        seed: int = 42,
    ) -> bytes:
        """POST a video and return the normal PNGs packed in a ZIP (bytes)."""
        with open(video_path, "rb") as fh:
            files = {"file": (os.path.basename(video_path), fh, "video/mp4")}
            data = {
                "max_res": str(max_res),
                "target_fps": str(target_fps),
                "window_size": str(window_size),
                "time_step_size": str(time_step_size),
                "decode_chunk_size": str(decode_chunk_size),
                "seed": str(seed),
            }
            r = requests.post(
                f"{self.base_url}/v1/normal-crafter",
                files=files, data=data, timeout=self.timeout)
        if r.status_code != 200:
            raise RuntimeError(
                f"server error {r.status_code}: {r.text[:500]}")
        return r.content


def unpack_normal_zip(payload: bytes, output_dir: str) -> int:
    """Extract a NormalCrafter ZIP of normal PNGs into `output_dir`.

    Returns the number of frames written.
    """
    os.makedirs(output_dir, exist_ok=True)
    n = 0
    with zipfile.ZipFile(io.BytesIO(payload)) as zf:
        for name in sorted(zf.namelist()):
            data = zf.read(name)
            path = os.path.join(output_dir, os.path.basename(name))
            with open(path, "wb") as fh:
                fh.write(data)
            n += 1
    return n


def run_client(
    base_url: str,
    input_path: str,
    output_dir: str,
    max_res: int = 1024,
    target_fps: int = 0,
    window_size: int = 14,
    time_step_size: int = 10,
    decode_chunk_size: int = 7,
    seed: int = 42,
    fps: int = 15,
    output_video: Optional[str] = None,
) -> Tuple[int, int, int]:
    """Bridging entry point used by the CLI.

    Returns (num_normal_frames, width, height).
    """
    was_dir = os.path.isdir(input_path)
    if was_dir:
        frame_paths = list_frame_paths(input_path)
        if not frame_paths:
            raise ValueError(f"no PNG frames found in {input_path}")
        eff_fps = target_fps if target_fps > 0 else fps
        tmpdir = tempfile.mkdtemp(prefix="normalcrafter_client_")
        video_path = os.path.join(tmpdir, "input.mp4")
        frames_to_video(frame_paths, eff_fps, video_path)
    else:
        if not is_video(input_path):
            raise ValueError(f"input is not a video or PNG dir: {input_path}")
        video_path = input_path

    try:
        payload = NormalCrafterClient(base_url).infer(
            video_path, max_res=max_res, target_fps=target_fps,
            window_size=window_size, time_step_size=time_step_size,
            decode_chunk_size=decode_chunk_size, seed=seed)
        n_frames = unpack_normal_zip(payload, output_dir)
        if output_video:
            _pngs_to_video(output_dir, output_video, eff_fps if was_dir else 30)
        return n_frames, 0, 0
    finally:
        if was_dir:
            import shutil

            shutil.rmtree(tmpdir, ignore_errors=True)


def _pngs_to_video(folder: str, out_video: str, fps: int) -> None:
    paths = list_frame_paths(folder)
    if not paths:
        return
    import cv2

    frame = cv2.imread(paths[0])
    h, w = frame.shape[:2]
    writer = cv2.VideoWriter(
        out_video, cv2.VideoWriter_fourcc(*"mp4v"), float(fps), (w, h))
    for p in paths:
        writer.write(cv2.imread(p))
    writer.release()
