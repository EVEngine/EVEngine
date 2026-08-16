#!/usr/bin/env bash
# Start the NormalCrafter GPU server (Linux/macOS). Requires NVIDIA GPU + deps.
# Usage: start_normal_crafter_server.sh [--port 8000]
set -euo pipefail
cd "$(dirname "$0")/.."
python -m server "$@"
