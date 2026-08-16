"""NormalCrafter: temporally consistent normal-map generation for frame sequences.

Deployable tool based on the ICCV 2025 NormalCrafter algorithm (Yanrui Bin et
al.).  This package is the **CPU client**: it bridges a PNG-frame sequence (or
video) to the GPU inference server and writes back a continuous, flicker-free
normal-map PNG sequence.  The GPU server (self-contained algorithm + FastAPI +
Docker) lives in `server/`.
"""

__version__ = "1.0.0"

SERVICE_NAME = "eve.normal.crafter"
DEFAULT_WINDOW_SIZE = 14
DEFAULT_TIME_STEP_SIZE = 10
DEFAULT_DECODE_CHUNK_SIZE = 7
DEFAULT_MAX_RES = 1024
DEFAULT_TARGET_FPS = 15
