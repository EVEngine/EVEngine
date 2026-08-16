# NormalCrafter GPU server image.
# Build from tools/normal-crafter (this directory is the Docker build context).
# Requires an NVIDIA GPU + CUDA runtime (see NVIDIA NGC PyTorch images).
FROM nvcr.io/nvidia/pytorch:24.02-py3

WORKDIR /app

# Server code + algorithm
COPY server/ /app/server/
COPY requirements-server.txt /app/requirements-server.txt

RUN pip install --no-cache-dir -r /app/requirements-server.txt \
    && python -c "import diffusers, fastapi, uvicorn; print('deps ok')"

# The fine-tuned weights are fetched at first run from HuggingFace.
# Optionally pre-bake them into the image for offline pods:
#   ARG MODEL_DIR
#   COPY ${MODEL_DIR} /app/models
#   ENV NORMAL_CRAFTER_UNET_REPO=/app/models/NormalCrafter
#   ENV NORMAL_CRAFTER_BASE_REPO=/app/models/svd

# Model is single-flight -> exactly 1 uvicorn worker.
EXPOSE 8000
CMD ["python", "-m", "server", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
