"""Run the NormalCrafter GPU inference server.

Usage:
    python -m server          # default: uvicorn on 0.0.0.0:8000
    python -m server --host 0.0.0.0 --port 8000
"""

from __future__ import annotations

import argparse
import os

import uvicorn


def main() -> None:
    parser = argparse.ArgumentParser(prog="normalcrafter-server")
    parser.add_argument("--host", default=os.environ.get("NORMAL_CRAFTER_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("NORMAL_CRAFTER_PORT", "8000")))
    parser.add_argument("--workers", type=int, default=1,
                        help="keep at 1 (model is single-flight)")
    args = parser.parse_args()
    uvicorn.run("server.app:app", host=args.host, port=args.port,
                workers=args.workers, log_level="info")


if __name__ == "__main__":
    main()
