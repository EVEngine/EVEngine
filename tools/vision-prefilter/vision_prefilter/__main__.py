"""CLI entry point: run the vision pre-filter HTTP service in the background.

Usage:
    python -m vision_prefilter [--host 0.0.0.0] [--port 8531]
                               [--model Qwen/Qwen2-VL-2B-Instruct]
                               [--eager-load] [--workers 1]
"""

from __future__ import annotations

import argparse
import os
import sys


def main() -> int:
    parser = argparse.ArgumentParser(prog="vision-prefilter", description=__doc__)
    parser.add_argument("--host", default=os.environ.get("VISION_PREFILTER_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("VISION_PREFILTER_PORT", "8531")))
    parser.add_argument("--model", default=os.environ.get("VISION_PREFILTER_MODEL", "Qwen/Qwen2-VL-2B-Instruct"))
    parser.add_argument("--eager-load", action="store_true", help="load model at startup")
    parser.add_argument("--workers", type=int, default=1, help="uvicorn workers (1 = single process/model)")
    parser.add_argument("--log-level", default="info")
    args = parser.parse_args()

    if args.eager_load:
        os.environ["VISION_PREFILTER_EAGER_LOAD"] = "1"
    os.environ.setdefault("VISION_PREFILTER_MODEL", args.model)

    import uvicorn

    uvicorn.run(
        "vision_prefilter.server:app",
        host=args.host,
        port=args.port,
        workers=args.workers,
        log_level=args.log_level,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
