#!/usr/bin/env bash
# Run NormalCrafter CPU client (Linux/macOS): sequence -> temporally consistent normals.
# Usage: run_normal_crafter.sh --server http://host:8000 --input <video|frames_dir> --output <out_dir>
set -euo pipefail
cd "$(dirname "$0")/.."
python -m normal_crafter "$@"
