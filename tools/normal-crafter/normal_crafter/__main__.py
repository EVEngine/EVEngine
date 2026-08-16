"""NormalCrafter client CLI: temporally consistent normals for frame sequences.

Deploys as a thin CPU client that sends a sequence (video or PNG dir) to the
NormalCrafter GPU server and writes back a normal-map PNG sequence.

Examples
--------
python -m normal_crafter --server http://127.0.0.1:8000 \
    --input frames/ --output out/normals/ --max-res 512

python -m normal_crafter --server http://gpu-01:8000 \
    --input shot.mp4 --output out/normals/ --output-video out/normals.mp4
"""

from __future__ import annotations

import argparse
import os
import sys

from . import __version__, SERVICE_NAME
from .client import NormalCrafterClient, run_client


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(
        prog="normal_crafter",
        description=f"{SERVICE_NAME} client v{__version__}",
    )
    parser.add_argument("--server", default=os.environ.get("NORMAL_CRAFTER_SERVER", "http://127.0.0.1:8000"),
                        help="GPU server base URL")
    parser.add_argument("--input", required=True, help="input video file OR PNG frame dir")
    parser.add_argument("--output", required=True, help="output directory for normal PNGs")
    parser.add_argument("--output-video", default=None, help="optional output normal mp4")
    parser.add_argument("--max-res", type=int, default=1024,
                        help="~20GB VRAM for 1024, ~6GB for 512")
    parser.add_argument("--target-fps", type=int, default=0,
                        help="resample FPS sent to server; <=0 keeps source")
    parser.add_argument("--fps", type=int, default=15,
                        help="FPS used when encoding a PNG dir to video")
    parser.add_argument("--window-size", type=int, default=14)
    parser.add_argument("--time-step-size", type=int, default=10)
    parser.add_argument("--decode-chunk-size", type=int, default=7)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--version", action="version", version=__version__)
    args = parser.parse_args(argv)

    client = NormalCrafterClient(args.server)
    if not client.health():
        print(f"[{SERVICE_NAME}] server not reachable at {args.server}", file=sys.stderr)
        sys.exit(2)

    n, _w, _h = run_client(
        base_url=args.server, input_path=args.input, output_dir=args.output,
        max_res=args.max_res, target_fps=args.target_fps,
        window_size=args.window_size, time_step_size=args.time_step_size,
        decode_chunk_size=args.decode_chunk_size, seed=args.seed,
        fps=args.fps, output_video=args.output_video,
    )
    print(f"[{SERVICE_NAME}] wrote {n} normal PNGs to {args.output}")


if __name__ == "__main__":
    main()
