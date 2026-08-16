"""EVEngine vision pre-filter.

Low-cost local VLM (Qwen2-VL-2B, 4bit) that batches render snapshots + a
compact geometry text snapshot and returns a strict JSON risk rating, filtering
out safe viewpoints before an expensive high-precision VLM is ever called.

Subpackages:
    protocol  - standardised request/response schemas + validation
    rules     - built-in business judgement rules (road occlusion / vegetation)
    model     - Qwen2-VL-2B 4bit loading + JSON-only generation
    server    - FastAPI HTTP service exposing batch inference
"""

__version__ = "0.1.0"

DEFAULT_PROTOCOL_VERSION = "1.0"
DEFAULT_SERVICE_NAME = "eve.vision.prefilter"
