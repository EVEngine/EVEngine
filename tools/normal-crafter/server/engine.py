"""Inference engine: single-flight GPU execution of the NormalCrafter algorithm.

A ~2B-param SVD UNet does not batch well across independent videos, so the
engine enforces **max-concurrency = 1** with a lock and a bounded queue.  The
model is preloaded once and reused for every request.
"""

from __future__ import annotations

import gc
import logging
import threading
from dataclasses import dataclass
from typing import List, Optional

import numpy as np

from .algorithm import NormalCrafterPipeline
from .model import build_pipeline

log = logging.getLogger("normalcrafter.engine")


@dataclass
class InferenceRequest:
    frames: List[np.ndarray]
    fps: int
    window_size: int = 14
    time_step_size: int = 10
    decode_chunk_size: int = 7
    seed: int = 42


class NormalCrafterEngine:
    """Owns the pipeline and serialises inference (single-flight)."""

    def __init__(self, pipe: Optional[NormalCrafterPipeline] = None,
                 **build_kwargs) -> None:
        self._lock = threading.Lock()
        self._pipe = pipe if pipe is not None else build_pipeline(**build_kwargs)
        self._warm = False

    @property
    def pipe(self) -> NormalCrafterPipeline:
        return self._pipe

    def warmup(self) -> None:
        """Force model materialisation / CUDA graph compilation once."""
        import torch
        from diffusers.training_utils import set_seed

        set_seed(42)
        probe = (np.ones((64, 64, 3), dtype=np.uint8) * 128)
        self._pipe([probe] * 8, window_size=8, time_step_size=8,
                   decode_chunk_size=4)
        torch.cuda.empty_cache()
        self._warm = True
        log.info("warmup complete")

    def infer(self, req: InferenceRequest) -> List[np.ndarray]:
        with self._lock:
            try:
                import torch
                from diffusers.training_utils import set_seed

                set_seed(req.seed)
                frames = self._pipe(
                    req.frames,
                    window_size=req.window_size,
                    time_step_size=req.time_step_size,
                    decode_chunk_size=req.decode_chunk_size,
                )
                torch.cuda.empty_cache()
                return list(frames)
            finally:
                gc.collect()
