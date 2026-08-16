#!/usr/bin/env bash
# Start the vision pre-filter service in the background (Linux/macOS).
# Usage: ./bin/start_vision_prefilter.sh [port]
set -euo pipefail

PORT="${1:-${VISION_PREFILTER_PORT:-8531}}"
cd "$(dirname "$0")/.."

if [[ -f vision-prefilter.pid ]] && kill -0 "$(cat vision-prefilter.pid)" 2>/dev/null; then
  echo "vision-prefilter already running (PID $(cat vision-prefilter.pid), port ${PORT})."
  exit 0
fi

nohup python -m vision_prefilter --host 127.0.0.1 --port "${PORT}" --eager-load \
  >> vision-prefilter.log 2>&1 &
echo $! > vision-prefilter.pid
echo "vision-prefilter starting on http://127.0.0.1:${PORT}  (log: vision-prefilter.log, PID: $!)"
