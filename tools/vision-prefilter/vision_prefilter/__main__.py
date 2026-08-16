"""CLI entry point: run the vision pre-filter HTTP service in the background.

Usage:
    python -m vision_prefilter [--host 127.0.0.1] [--port 8531]
                               [--backend http://127.0.0.1:8080] [--workers 1]
"""

from __future__ import annotations

import argparse
import os
import sys


def main() -> int:
    parser = argparse.ArgumentParser(prog="vision-prefilter", description=__doc__)
    parser.add_argument("--host", default=os.environ.get("VISION_PREFILTER_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("VISION_PREFILTER_PORT", "8531")))
    parser.add_argument(
        "--backend",
        default=os.environ.get("VISION_PREFILTER_BACKEND", "http://127.0.0.1:8080"),
        help="llama-server OpenAI-compatible base URL.",
    )
    parser.add_argument("--workers", type=int, default=1, help="uvicorn workers")
    parser.add_argument("--log-level", default="info")
    args = parser.parse_args()

    os.environ["VISION_PREFILTER_BACKEND"] = args.backend

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
