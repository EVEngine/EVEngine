# Local agent handoff — upstream VKBuilder PR

## Goal
Push the prepared APIs to `sunxfancy/VKBuilder` and open a PR against `master`.
Cloud agent `cursor[bot]` cannot push (GitHub App only has `EVEngine/EVEngine`).

## What to land
- `ColorAttachmentImage` `extraUsage` + `Device::createCopyableColorTarget`
- `CubemapRenderTarget` + `Device::createCubemapRenderTarget`
- `recordImageToBufferReadback`
- header-only `include/vkbuilder/frame_arena.hpp`

Tracking issue (patch embedded): https://github.com/sunxfancy/VKBuilder/issues/5  
Engine consumer PR (overrides until pin moves): https://github.com/EVEngine/EVEngine/pull/682

## Preferred apply path (git am)

```sh
git clone https://github.com/sunxfancy/VKBuilder.git
cd VKBuilder
git checkout -b cursor/renderable-images-arena-7d75
# Obtain the patch from the cloud artifacts, or from issue #5, then:
git am 0001-Add-copyable-color-targets-CubemapRenderTarget-Frame.patch
git push -u origin cursor/renderable-images-arena-7d75
gh pr create --repo sunxfancy/VKBuilder --base master \
  --head cursor/renderable-images-arena-7d75 \
  --title "Add copyable color targets, CubemapRenderTarget, FrameArena, and readback" \
  --body "$(cat <<'EOF'
## Summary
- `createCopyableColorTarget` / `ColorAttachmentImage` `extraUsage` for blit/copy scene color and Canvas
- `CubemapRenderTarget` (+ factory) for HDR / IBL bake without raw `UniqueImage`
- `recordImageToBufferReadback` helper
- header-only `vkbuilder/frame_arena.hpp` per-frame bump allocator

Prepared for EVEngine render migration: https://github.com/EVEngine/EVEngine/pull/682
Tracking: https://github.com/sunxfancy/VKBuilder/issues/5

## Test plan
- [ ] Build EVEngine against this branch (or copy headers into `cmake/overrides/vkbuilder` and `make debug`)
- [ ] Filtered graphics tests / existing VKBuilder consumers still compile
EOF
)"
```

## Alternate: git bundle

```sh
git clone https://github.com/sunxfancy/VKBuilder.git
cd VKBuilder
git fetch /path/to/vkbuilder-renderable-images-arena-7d75.bundle cursor/renderable-images-arena-7d75:cursor/renderable-images-arena-7d75
git checkout cursor/renderable-images-arena-7d75
git push -u origin cursor/renderable-images-arena-7d75
# then gh pr create as above
```

Expected tip: `9735e89974ce001645a264b3ea350e5a9a28c9a3` (+271 / −5).

## After upstream merges
On EVEngine: bump `external/VKBuilder` pin, delete `cmake/overrides/vkbuilder/`, keep the engine migration call sites.

## Optional: let the cloud agent push next time
On this machine run `agent login` (if needed) then `agent worker start`, and add `sunxfancy/VKBuilder` to the Cursor GitHub App install — or set secret `VKBUILDER_GITHUB_TOKEN`.
