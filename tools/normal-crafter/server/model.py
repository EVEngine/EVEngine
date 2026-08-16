"""Model loading for the NormalCrafter GPU server.

Preloads the fine-tuned NormalCrafter weights (UNet + temporal VAE from the HF
repo) onto the stock SVD img2vid-xt architecture and builds the inference
pipeline.  Run once at process start and keep warm for the lifetime of the pod.
"""

from __future__ import annotations

import os

from .algorithm import NormalCrafterPipeline

# Default repos: fine-tuned NormalCrafter weights + SVD base components.
DEFAULT_UNET_REPO = os.environ.get("NORMAL_CRAFTER_UNET_REPO", "Yanrui95/NormalCrafter")
DEFAULT_BASE_REPO = os.environ.get(
    "NORMAL_CRAFTER_BASE_REPO",
    "stabilityai/stable-video-diffusion-img2vid-xt",
)


def build_pipeline(
    unet_repo: str = DEFAULT_UNET_REPO,
    base_repo: str = DEFAULT_BASE_REPO,
    cpu_offload: str = "model",
    weight_dtype: str = "float16",
    local_files_only: bool = False,
) -> NormalCrafterPipeline:
    """Build a ready-to-run NormalCrafterPipeline (GPU)."""
    import torch
    from diffusers import AutoencoderKLTemporalDecoder
    from diffusers import UNetSpatioTemporalConditionModel

    kwargs = {"local_files_only": local_files_only} if local_files_only else {}
    unet = UNetSpatioTemporalConditionModel.from_pretrained(
        unet_repo, subfolder="unet", low_cpu_mem_usage=True, **kwargs)
    vae = AutoencoderKLTemporalDecoder.from_pretrained(
        unet_repo, subfolder="vae", **kwargs)
    dtype = torch.float16 if weight_dtype == "float16" else torch.float32
    vae.to(dtype=dtype)
    unet.to(dtype=dtype)

    pipe = NormalCrafterPipeline.from_pretrained(
        base_repo, unet=unet, vae=vae, torch_dtype=dtype,
        variant="fp16", **kwargs)

    if cpu_offload == "sequential":
        pipe.enable_sequential_cpu_offload()
    elif cpu_offload == "model":
        pipe.enable_model_cpu_offload()
    elif cpu_offload == "none":
        pipe.to("cuda")
    else:
        raise ValueError(f"unknown cpu_offload: {cpu_offload}")

    try:
        pipe.enable_xformers_memory_efficient_attention()
    except Exception:
        pass
    return pipe
