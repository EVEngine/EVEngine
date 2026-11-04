# Worker 4 defect log

Defects identified during API and implementation review follow. Runtime reproduction status is stated separately.

## VirtualTextureStreaming permits taking an already taken upload batch

- Input: after `recordFeedback(0, 0)`, `update()`, and one successful `takePendingUploads()`, call `takePendingUploads()` again before commit or abandon.
- Expected: `VirtualTextureStreaming.h` documents failure while a previous batch is awaiting commit or abandon.
- Actual: `VirtualTextureStreaming.cpp` leaves `pending_` engaged with an empty sentinel, so the second call succeeds and returns an empty batch.
- Source: `src/modules/graphics/VirtualTextureStreaming.cpp`, `takePendingUploads()`.
- Reproduction: code-path confirmed by reading the sentinel assignment and the next call's precondition; runtime reproduction remains pending. The test suite does not assert the unmet promise while this defect remains open.
