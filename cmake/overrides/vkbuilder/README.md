# VKBuilder engine overrides

Copied over the pinned submodule tree by `eve_prepare_vkbuilder_headers`
(before EVEngine's existing string patches).

Holds APIs staged for upstream sunxfancy/VKBuilder (push access was unavailable
from this agent environment):

- `vkbuilder.hpp` — copyable color targets, `CubemapRenderTarget`,
  `recordImageToBufferReadback`
- `vkbuilder/frame_arena.hpp` — per-frame bump allocator

When these land upstream and the submodule pin advances, delete this directory.
