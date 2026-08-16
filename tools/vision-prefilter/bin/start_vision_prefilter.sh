#!/usr/bin/env bash
# Start the vision pre-filter stack in the background (Linux/macOS).
#   1. llama-server (llama.cpp) serving a Qwen2-VL-2B GGUF model
#   2. the vision-prefilter FastAPI gateway in front of it
# Usage: ./bin/start_vision_prefilter.sh [path/to/model.gguf]
set -euo pipefail

MODEL_GGUF="${1:-${VISION_PREFILTER_GGUF:-}}"
if [[ -z "${MODEL_GGUF}" ]]; then
  echo "ERROR: provide a Qwen2-VL-2B GGUF model path (arg or VISION_PREFILTER_GGUF)." >&2
  exit 1
fi
[[ -f "${MODEL_GGUF}" ]] || { echo "ERROR: model file not found: ${MODEL_GGUF}" >&2; exit 1; }

LLAMA_PORT="${LLAMA_PORT:-8080}"
GW_PORT="${VISION_PREFILTER_PORT:-8531}"
cd "$(dirname "$0")/.."

stop() {
  [[ -f vision-prefilter.pid ]] && kill "$(cat vision-prefilter.pid)" 2>/dev/null || true
  [[ -f llama-server.pid ]] && kill "$(cat llama-server.pid)" 2>/dev/null || true
}
trap stop EXIT

if [[ -f llama-server.pid ]] && kill -0 "$(cat llama-server.pid)" 2>/dev/null; then
  echo "llama-server already running (PID $(cat llama-server.pid))."
else
  llama-server -m "${MODEL_GGUF}" --host 127.0.0.1 --port "${LLAMA_PORT}" \
    -ngl 99 --no-warmup >> llama-server.log 2>&1 &
  echo $! > llama-server.pid
  echo "llama-server starting on http://127.0.0.1:${LLAMA_PORT} (log: llama-server.log)"
  # Wait for the model to load.
  for _ in $(seq 1 120); do
    curl -sf "http://127.0.0.1:${LLAMA_PORT}/health" >/dev/null 2>&1 && break
    sleep 1
  done
fi

if [[ -f vision-prefilter.pid ]] && kill -0 "$(cat vision-prefilter.pid)" 2>/dev/null; then
  echo "vision-prefilter already running (PID $(cat vision-prefilter.pid))."
  exit 0
fi

VISION_PREFILTER_BACKEND="http://127.0.0.1:${LLAMA_PORT}" \
  nohup python -m vision_prefilter --host 127.0.0.1 --port "${GW_PORT}" \
    --backend "http://127.0.0.1:${LLAMA_PORT}" >> vision-prefilter.log 2>&1 &
echo $! > vision-prefilter.pid
echo "vision-prefilter starting on http://127.0.0.1:${GW_PORT}  (log: vision-prefilter.log, PID: $(cat vision-prefilter.pid))"
