"""FastAPI GPU inference service for NormalCrafter.

Deployment unit: a single GPU pod that owns the model, accepts an uploaded video
(or encoded frame sequence), runs the temporally-consistent normal inference,
and returns the normal-map frames as PNGs packed in a ZIP (lossless).

The GPU pod is kept **stateless** and **single-flight**; all frame <-> video
bridging and PNG I/O lives on the CPU client.
"""

from __future__ import annotations

import io
import logging
import os
import tempfile
import zipfile
from contextlib import asynccontextmanager
from typing import List, Optional

import cv2
import numpy as np
from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.responses import Response

from .engine import InferenceRequest, NormalCrafterEngine
from .model import build_pipeline

log = logging.getLogger("normalcrafter.server")

# Tunable via env so the pod config is explicit, not per-request.
WINDOW_SIZE = int(os.environ.get("NORMAL_CRAFTER_WINDOW_SIZE", "14"))
TIME_STEP_SIZE = int(os.environ.get("NORMAL_CRAFTER_TIME_STEP_SIZE", "10"))
DECODE_CHUNK_SIZE = int(os.environ.get("NORMAL_CRAFTER_DECODE_CHUNK_SIZE", "7"))
DEFAULT_MAX_RES = int(os.environ.get("NORMAL_CRAFTER_MAX_RES", "1024"))
CPU_OFFLOAD = os.environ.get("NORMAL_CRAFTER_CPU_OFFLOAD", "model")
WEIGHT_DTYPE = os.environ.get("NORMAL_CRAFTER_WEIGHT_DTYPE", "float16")

_engine: Optional[NormalCrafterEngine] = None


@asynccontextmanager
async def lifespan(_: FastAPI):
    global _engine
    log.info("building NormalCrafter pipeline ...")
    _engine = NormalCrafterEngine(
        cpu_offload=CPU_OFFLOAD, weight_dtype=WEIGHT_DTYPE)
    if os.environ.get("NORMAL_CRAFTER_WARMUP", "1") == "1":
        _engine.warmup()
    log.info("NormalCrafter server ready")
    yield
    _engine = None


app = FastAPI(title="NormalCrafter", version="1.0.0", lifespan=lifespan)


def _decode_video(upload: UploadFile, max_res: int, target_fps: int) -> List[np.ndarray]:
    """Read an uploaded video into RGB frames, resized to fit `max_res`."""
    with tempfile.NamedTemporaryFile(suffix=".mp4", delete=False) as tmp:
        tmp.write(upload.file.read())
        tmp_path = tmp.name
    try:
        cap = cv2.VideoCapture(tmp_path)
        if not cap.isOpened():
            raise ValueError("unable to decode uploaded video")
        frames: List[np.ndarray] = []
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            h, w = frame.shape[:2]
            scale = min(1.0, max_res / max(h, w)) if max_res else 1.0
            if scale < 1.0:
                frame = cv2.resize(frame, (int(w * scale), int(h * scale)),
                                   interpolation=cv2.INTER_AREA)
            frames.append(frame)
        cap.release()
        return frames
    finally:
        os.unlink(tmp_path)


def _frames_to_zip(frames: List[np.ndarray]) -> bytes:
    bio = io.BytesIO()
    with zipfile.ZipFile(bio, "w", zipfile.ZIP_STORED) as zf:
        for i, f in enumerate(frames):
            rgb = ((f + 1.0) / 2.0 * 255.0).round().astype(np.uint8)
            ok, buf = cv2.imencode(".png", cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR))
            zf.writestr(f"frame_{i + 1:06d}.png", buf.tobytes())
    return bio.getvalue()


@app.get("/health")
def health():
    return {"service": "eve.normal.crafter", "status": "ok",
            "engine_ready": _engine is not None}


@app.post("/v1/normal-crafter")
async def normal_crafter(
    file: UploadFile = File(...),
    max_res: int = Form(DEFAULT_MAX_RES),
    target_fps: int = Form(0),
    window_size: int = Form(WINDOW_SIZE),
    time_step_size: int = Form(TIME_STEP_SIZE),
    decode_chunk_size: int = Form(DECODE_CHUNK_SIZE),
    seed: int = Form(42),
):
    if _engine is None:
        raise HTTPException(503, "engine not ready")
    frames = _decode_video(file, max_res, target_fps)
    if not frames:
        raise HTTPException(400, "empty video")
    if len(frames) < window_size:
        raise HTTPException(
            400, f"video has {len(frames)} frames < window_size {window_size}")
    normals = _engine.infer(InferenceRequest(
        frames=frames, fps=target_fps or 30, window_size=window_size,
        time_step_size=time_step_size, decode_chunk_size=decode_chunk_size,
        seed=seed))
    payload = _frames_to_zip(normals)
    h, w = normals[0].shape[:2]
    return Response(
        content=payload,
        media_type="application/zip",
        headers={
            "X-Normal-Frames": str(len(normals)),
            "X-Normal-Width": str(w),
            "X-Normal-Height": str(h),
        },
    )
