"""NormalCrafter inference algorithm (self-contained server implementation).

Self-contained reimplementation of the ICCV 2025 NormalCrafter inference
algorithm (Yanrui Bin et al., https://arxiv.org/abs/2504.11427), based on the
reference implementation (MIT-licensed) from github.com/Binyr/NormalCrafter.

It does NOT import the upstream `normalcrafter` package: the algorithm is
implemented here on top of stock `diffusers`.  Inference is a **single latent
denoise pass** (1 step, CFG off, zero-initialised latents); long sequences are
handled by temporal **windowing + linear blending**, which is exactly what makes
the resulting normals continuous and flicker-free across frames.

Notes
-----
* During inference the upstream patched UNet runs with `dino_down_block_res_samples
  = None`, so it reduces to stock `diffusers.UNetSpatioTemporalConditionModel`
  (SVD img2vid-xt).  We load the fine-tuned UNet/VAE weights onto that stock
  architecture and do not vendor the (training-only) DINO hook.
* Normal outputs are in range [-1, 1] (RGB); convert with `(n + 1) / 2` for PNG.
"""

from __future__ import annotations

from typing import List, Optional, Tuple, Union

import numpy as np
import torch
import torch.nn.functional as F
from diffusers import StableVideoDiffusionPipeline
from diffusers.utils import BaseOutput


class NormalCrafterPipeline(StableVideoDiffusionPipeline):
    """SVD-based pipeline producing temporally consistent normal sequences."""

    # ------------------------------------------------------------------ #
    # Image embedding
    # ------------------------------------------------------------------ #
    def _encode_image(
        self, image, device, num_videos_per_prompt, do_classifier_free_guidance,
        scale: int = 1, image_size: Optional[Tuple[int, int]] = None,
    ):
        dtype = next(self.image_encoder.parameters()).dtype
        if not isinstance(image, torch.Tensor):
            image = self.video_processor.pil_to_numpy(image)   # (0,255) -> (0,1)
            image = self.video_processor.numpy_to_pt(image)    # (n,h,w,c) -> (n,c,h,w)
            pixel_values = image
            patches = [pixel_values]
            for i in range(1, scale):
                num_patches = i + 1
                patch_h = pixel_values.shape[2] // num_patches + 1
                patch_w = pixel_values.shape[3] // num_patches + 1
                for j in range(num_patches):
                    for k in range(num_patches):
                        patches.append(
                            pixel_values[:, :,
                                         j * patch_h:(j + 1) * patch_h,
                                         k * patch_w:(k + 1) * patch_w])

            def encode_image(img):
                img = img * 2.0 - 1.0
                img = _resize_with_antialiasing(
                    img, image_size if image_size is not None else (224, 224))
                img = (img + 1.0) / 2.0
                px = self.feature_extractor(
                    images=img, do_normalize=True, do_center_crop=False,
                    do_resize=False, do_rescale=False, return_tensors="pt",
                ).pixel_values.to(device=device, dtype=dtype)
                emb = self.image_encoder(px).image_embeds
                if len(emb.shape) < 3:
                    emb = emb.unsqueeze(1)
                return emb

            image_embeddings = torch.cat(
                [encode_image(p) for p in patches], dim=1)

        bs, seq_len, _ = image_embeddings.shape
        image_embeddings = image_embeddings.repeat(1, num_videos_per_prompt, 1)
        image_embeddings = image_embeddings.view(
            bs * num_videos_per_prompt, seq_len, -1)
        if do_classifier_free_guidance:
            negative = torch.zeros_like(image_embeddings)
            image_embeddings = torch.cat([negative, image_embeddings])
        return image_embeddings

    # ------------------------------------------------------------------ #
    # VAE encode
    # ------------------------------------------------------------------ #
    def _encode_video_vae(self, images, chunk_size: int = 14):
        if isinstance(images, list):
            width, height = images[0].size
        else:
            height, width = images[0].shape[:2]
        needs_upcast = (self.vae.dtype == torch.float16
                        and self.vae.config.force_upcast)
        if needs_upcast:
            self.vae.to(dtype=torch.float32)
        device = self._execution_device
        images = self.video_processor.preprocess_video(
            images, height=height, width=width).to(device, self.vae.dtype)
        images = images.squeeze(0).permute(1, 0, 2, 3)   # (t,c,h,w)

        latents = []
        for i in range(0, images.shape[0], chunk_size):
            latents.append(
                self.vae.encode(images[i:i + chunk_size]).latent_dist.mode())
        image_latents = torch.cat(latents)
        if needs_upcast:
            self.vae.to(dtype=torch.float16)
        return image_latents

    # ------------------------------------------------------------------ #
    # Pad / unpad to multiples of 64
    # ------------------------------------------------------------------ #
    @staticmethod
    def _pad(images, scale: int = 64):
        def get_pad(new, old):
            pad = (new - old) // 2
            return [pad, pad + 1] if old % 2 == 1 else [pad, pad]

        if isinstance(images[0], np.ndarray):
            h, w = images[0].shape[:2]
        else:
            w, h = images[0].size
        if w % scale == 0 and h % scale == 0:
            return images, None
        new_w = int(np.ceil(w / scale) * scale)
        new_h = int(np.ceil(h / scale) * scale)
        # pads ordered (t, b, l, r) to match `_unpad`
        pads = get_pad(new_h, h) + get_pad(new_w, w)
        import cv2
        out = []
        for image in images:
            if isinstance(image, np.ndarray):
                out.append(cv2.copyMakeBorder(
                    image, pads[0], pads[1], pads[2], pads[3],
                    cv2.BORDER_CONSTANT, value=(1., 1., 1.)))
            else:
                img = np.array(image)
                img = cv2.copyMakeBorder(
                    img, pads[0], pads[1], pads[2], pads[3],
                    cv2.BORDER_CONSTANT, value=(255, 255, 255))
                from PIL import Image
                out.append(Image.fromarray(img))
        return out, pads

    @staticmethod
    def _unpad(v, pads):
        if pads is None:
            return v
        t, b, l, r = pads
        if t > 0 or b > 0:
            v = v[:, :, t:(-b if b > 0 else None)]
        if l > 0 or r > 0:
            v = v[:, :, :, l:(-r if r > 0 else None)]
        return v

    # ------------------------------------------------------------------ #
    # Main inference
    # ------------------------------------------------------------------ #
    @torch.no_grad()
    def __call__(
        self,
        images,
        decode_chunk_size: int = 7,
        time_step_size: int = 10,
        window_size: int = 14,
        generator: Optional[torch.Generator] = None,
    ):
        images, pads = self._pad(images)
        width, height = images[0].size
        num_frames = len(images)
        self.check_inputs(images, height, width)

        batch_size = 1
        device = self._execution_device
        self._guidance_scale = 1.0
        num_inference_steps = 1
        do_classifier_free_guidance = False
        num_videos_per_prompt = 1
        fps, motion_bucket_id, noise_aug_strength = 7, 127, 0.0
        determine_train = True
        use_linear_merge = True

        # 1. CLIP embedding of the first frame
        image_embeddings = self._encode_image(
            images, device, num_videos_per_prompt, do_classifier_free_guidance,
            scale=1, image_size=None)
        # 2. VAE-encode input frames
        image_latents = self._encode_video_vae(
            images, chunk_size=decode_chunk_size).to(image_embeddings.dtype)
        image_latents = image_latents.unsqueeze(0)   # (1,t,c,h,w)

        # 3. Added time ids
        added_time_ids = self._get_add_time_ids(
            fps, motion_bucket_id, noise_aug_strength, image_embeddings.dtype,
            batch_size, num_videos_per_prompt, do_classifier_free_guidance,
        ).to(device)

        # 4. Build sliding windows
        def get_ses(n):
            ses = [[i, i + window_size] for i in range(0, n, time_step_size)]
            ses = [se for se in ses if se[1] <= n]
            if ses[-1][1] < n:
                ses.append([n - window_size, n])
            return ses

        windows = get_ses(num_frames)
        pred = None

        for i, (s, e) in enumerate(windows):
            win_emb = image_embeddings[s:e]
            win_latents = image_latents[:, s:e]
            if i == 0 or time_step_size == window_size:
                to_replace = None
            else:
                last_s, last_e = windows[i - 1]
                n_replace = last_e - s
                to_replace = pred[:, -n_replace:]

            latents = self._generate(
                num_inference_steps, device, batch_size, num_videos_per_prompt,
                window_size, height, width, win_emb, generator,
                determine_train, to_replace, do_classifier_free_guidance,
                win_latents, added_time_ids)

            if to_replace is not None and use_linear_merge:
                n = to_replace.shape[1]
                weight = torch.linspace(1., 0., n + 2)[1:-1].to(device)
                weight = weight[None, :, None, None, None]
                latents[:, :n] = (to_replace * weight
                                  + latents[:, :n] * (1 - weight))

            pred = latents if pred is None \
                else torch.cat([pred[:, :s], latents], dim=1)

        # 5. VAE decode
        needs_upcast = (self.vae.dtype == torch.float16
                        and self.vae.config.force_upcast)
        if needs_upcast:
            self.vae.to(dtype=torch.float16)
        frames = self.decode_latents(pred, num_frames, decode_chunk_size)
        frames = self.video_processor.postprocess_video(
            video=frames, output_type="np") * 2 - 1   # -> [-1,1]
        frames = self._unpad(frames, pads)
        self.maybe_free_model_hooks()
        return frames

    # ------------------------------------------------------------------ #
    # Single denoise step
    # ------------------------------------------------------------------ #
    def _generate(self, num_inference_steps, device, batch_size,
                  num_videos_per_prompt, num_frames, height, width,
                  image_embeddings, generator, determine_train, to_replace,
                  do_classifier_free_guidance, image_latents, added_time_ids):
        self.scheduler.set_timesteps(num_inference_steps, device=device)
        timesteps = self.scheduler.timesteps

        num_channels = self.unet.config.in_channels
        latents = self.prepare_latents(
            batch_size * num_videos_per_prompt, num_frames, num_channels,
            height, width, image_embeddings.dtype, device, generator, None)
        if determine_train:
            latents[...] = 0.

        for t in timesteps:
            if to_replace is not None:
                n = to_replace.shape[1]
                if determine_train:
                    latents[:, :n] = to_replace
                else:
                    from diffusers.utils.torch_utils import randn_tensor
                    noise = randn_tensor(to_replace.shape, generator=generator,
                                         device=device,
                                         dtype=image_embeddings.dtype)
                    latents[:, :n] = self.scheduler.add_noise(
                        to_replace, noise, t.unsqueeze(0))

            model_input = latents if not do_classifier_free_guidance \
                else torch.cat([latents] * 2)
            model_input = self.scheduler.scale_model_input(model_input, t)
            model_input = torch.cat([model_input, image_latents], dim=2)

            noise_pred = self.unet(
                model_input, t, encoder_hidden_states=image_embeddings,
                added_time_ids=added_time_ids, return_dict=False)[0]

            if do_classifier_free_guidance:
                uncond, cond = noise_pred.chunk(2)
                noise_pred = uncond + self.guidance_scale * (cond - uncond)

            latents = self.scheduler.step(noise_pred, t, latents).prev_sample
        return latents


class NormalCrafterOutput(BaseOutput):
    frames: List[np.ndarray]


# ---------------------------------------------------------------------- #
# Antialiasing resize helpers (from the reference implementation)
# ---------------------------------------------------------------------- #
def _resize_with_antialiasing(input, size, interpolation="bicubic",
                              align_corners=True):
    h, w = input.shape[-2:]
    factors = (h / size[0], w / size[1])
    sigmas = (max((factors[0] - 1.0) / 2.0, 0.001),
              max((factors[1] - 1.0) / 2.0, 0.001))
    ks = (int(max(2.0 * 2 * sigmas[0], 3)),
          int(max(2.0 * 2 * sigmas[1], 3)))
    if ks[0] % 2 == 0:
        ks = (ks[0] + 1, ks[1])
    if ks[1] % 2 == 0:
        ks = (ks[0], ks[1] + 1)
    input = _gaussian_blur2d(input, ks, sigmas)
    return F.interpolate(input, size=size, mode=interpolation,
                         align_corners=align_corners)


def _compute_padding(kernel_size):
    computed = [k - 1 for k in kernel_size]
    out = 2 * len(kernel_size) * [0]
    for i in range(len(kernel_size)):
        tmp = computed[-(i + 1)]
        front = tmp // 2
        rear = tmp - front
        out[2 * i + 0] = front
        out[2 * i + 1] = rear
    return out


def _filter2d(input, kernel):
    b, c, h, w = input.shape
    tmp = kernel[:, None, ...].to(device=input.device, dtype=input.dtype)
    tmp = tmp.expand(-1, c, -1, -1)
    height, width = tmp.shape[-2:]
    padding = _compute_padding([height, width])
    input = F.pad(input, padding, mode="reflect")
    tmp = tmp.reshape(-1, 1, height, width)
    input = input.view(-1, tmp.size(0), input.size(-2), input.size(-1))
    output = F.conv2d(input, tmp, groups=tmp.size(0), padding=0, stride=1)
    return output.view(b, c, h, w)


def _gaussian(window_size, sigma):
    if isinstance(sigma, float):
        sigma = torch.tensor([[sigma]])
    batch_size = sigma.shape[0]
    x = (torch.arange(window_size, device=sigma.device, dtype=sigma.dtype)
         - window_size // 2).expand(batch_size, -1)
    if window_size % 2 == 0:
        x = x + 0.5
    gauss = torch.exp(-x.pow(2.0) / (2 * sigma.pow(2.0)))
    return gauss / gauss.sum(-1, keepdim=True)


def _gaussian_blur2d(input, kernel_size, sigma):
    if isinstance(sigma, tuple):
        sigma = torch.tensor([sigma], dtype=input.dtype)
    else:
        sigma = sigma.to(dtype=input.dtype)
    ky, kx = int(kernel_size[0]), int(kernel_size[1])
    bs = sigma.shape[0]
    kx_g = _gaussian(kx, sigma[:, 1].view(bs, 1))
    ky_g = _gaussian(ky, sigma[:, 0].view(bs, 1))
    out_x = _filter2d(input, kx_g[..., None, :])
    return _filter2d(out_x, ky_g[..., None])
