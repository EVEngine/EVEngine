#!/usr/bin/env bash
# Download a Qwen2-VL-2B GGUF (Q4_K_M) model for use with llama.cpp.
# Usage: ./scripts/download_model.sh [output-dir]
set -euo pipefail

OUT_DIR="${1:-models}"
mkdir -p "${OUT_DIR}"

# Default to a well-known community Q4_K_M GGUF of Qwen2-VL-2B-Instruct.
MODEL_URL="${VISION_PREFILTER_GGUF_URL:-https://huggingface.co/Qwen/Qwen2-VL-2B-Instruct-GGUF/resolve/main/qwen2-vl-2b-instruct-q4_k_m.gguf}"
OUT_FILE="${OUT_DIR}/qwen2-vl-2b-instruct-q4_k_m.gguf"

echo "Downloading ${MODEL_URL}"
echo "  -> ${OUT_FILE}"
curl -L --fail --progress-bar "${MODEL_URL}" -o "${OUT_FILE}"
echo "Done. Start the stack with:"
echo "  bin/start_vision_prefilter.sh ${OUT_FILE}"
