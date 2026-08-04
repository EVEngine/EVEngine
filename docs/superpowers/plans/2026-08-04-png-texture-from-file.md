# PNG Texture From File Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `Graphics::newTextureFromFile`, fix ImageData module lookup so PNG encode/decode works, and smoke-test via a runtime-written temporary PNG (no assets in repo).

**Architecture:** Convenience API on Vulkan `Graphics` composes existing `Filesystem::read` → `Image::newImageData` → `newTexture(ImageData*)`. Temporary PNG round-trip lives only in the gtest. ImageData currently looks up modules as `"image"` / `"filesystem"` but registrations use `"Image"` / `"Filesystem"` — that must be fixed or encode/decode always fails.

**Tech Stack:** C++20, PhysFS (`EVFileSystem`), medialoader PNG (`EVImage`), Vulkan/`VKBuilder` (`EVGraphics`), GoogleTest.

**Spec:** [docs/superpowers/specs/2026-08-04-png-texture-from-file-design.md](../specs/2026-08-04-png-texture-from-file-design.md)

## Global Constraints

- Scope is **A only** (file PNG → Texture). No Quad UV, Camera2D, or Squirrel bindings.
- Do **not** add `test/assets/*.png` (or any checked-in PNG).
- Texture ownership unchanged: Graphics owns returned `Texture*`.
- Failures throw `eve::Exception` (no null returns).
- Do **not** create git commits unless the user explicitly asks (this repo’s commit rule overrides “frequent commits” in the skill template).
- Build/test on Windows Debug with Vulkan SDK on PATH:
  - `$env:VULKAN_SDK = "C:\VulkanSDK\1.4.357.0"`
  - `$env:Path = "$env:VULKAN_SDK\Bin;$env:Path"`
  - `cmake --build build/win32-debug --config Debug --target unit_test -j 8`

## File Structure

| File | Role |
|------|------|
| `src/modules/image/ImageData.cpp` | Fix `Image` / `Filesystem` module lookup in `decode` + `encode` |
| `src/modules/graphics/Graphics.h` | Declare `newTextureFromFile` |
| `src/modules/graphics/vulkan/Graphics.h` | Override declaration |
| `src/modules/graphics/vulkan/Graphics.cpp` | Implement load → decode → upload |
| `src/engine/CMakeLists.txt` | Link `EVFileSystem` into `eve` (needed by convenience API) |
| `test/RenderSystem.cpp` | Smoke + `EXPECT_THROW` for missing file |

---

### Task 1: Fix ImageData module name lookups

**Files:**
- Modify: `src/modules/image/ImageData.cpp` (decode ~line 101, encode ~lines 158–194)
- Test: covered by Task 3 smoke (encode+decode); optional early compile of `unit_test`

**Interfaces:**
- Consumes: `eve::image::Image::create()`, `eve::filesystem::Filesystem::create()` (already exist)
- Produces: working `ImageData(Data*)` decode and `ImageData::encode(..., writefile=true)` when modules are creatable

**Why:** `Module_IMPL` registers names `"Image"` and `"Filesystem"`, but ImageData uses `"image"` / `"filesystem"`, so `getInstance` returns null and encode/decode throw “must be loaded”.

- [ ] **Step 1: Patch decode lookup**

In `ImageData::decode`, replace:

```cpp
auto module = ModuleManager::getInstance<Image>("image");
```

with:

```cpp
auto module = Image::create();
```

Keep the null check (should not fire after `create()`, but leave a clear exception if it does).

- [ ] **Step 2: Patch encode lookups**

In `ImageData::encode`, replace:

```cpp
auto module = ModuleManager::getInstance<Image>("image");
```

with:

```cpp
auto module = Image::create();
```

Replace:

```cpp
auto fs = ModuleManager::getInstance<eve::filesystem::Filesystem>("filesystem");
```

with:

```cpp
auto fs = eve::filesystem::Filesystem::create();
```

`Filesystem.h` is already reachable via existing includes in this translation unit (through `filesystem/Filesystem.h` used by encode). If the compiler complains, add `#include "filesystem/Filesystem.h"`.

- [ ] **Step 3: Rebuild EVImage / unit_test**

```powershell
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.357.0"
$env:Path = "$env:VULKAN_SDK\Bin;$env:Path"
cd c:\Users\xiaofans\Workspace\EVEngine
cmake --build build/win32-debug --config Debug --target unit_test -j 8
```

Expected: build succeeds (no behavior change asserted yet).

---

### Task 2: Add `Graphics::newTextureFromFile`

**Files:**
- Modify: `src/modules/graphics/Graphics.h` (after `newTexture(image::ImageData *)`)
- Modify: `src/modules/graphics/vulkan/Graphics.h`
- Modify: `src/modules/graphics/vulkan/Graphics.cpp`
- Modify: `src/engine/CMakeLists.txt` (add `EVFileSystem` to `EVELIBS`)

**Interfaces:**
- Consumes: `filesystem::Filesystem::create()->read(filename)`, `image::Image::create()->newImageData(Data*)`, `Graphics::newTexture(ImageData*)`
- Produces: `virtual Texture *newTextureFromFile(const std::string &filename) = 0` on public Graphics; Vulkan override

- [ ] **Step 1: Declare on public Graphics**

In `src/modules/graphics/Graphics.h`, ensure `#include <string>` is present (add if missing). After `newTexture(image::ImageData *data)`, add:

```cpp
/** Load file via Filesystem + Image decode, then upload (RGBA8). Throws on failure. */
virtual Texture *newTextureFromFile(const std::string &filename) = 0;
```

- [ ] **Step 2: Declare Vulkan override**

In `src/modules/graphics/vulkan/Graphics.h`, next to other `newTexture` overrides:

```cpp
Texture *newTextureFromFile(const std::string &filename) override;
```

- [ ] **Step 3: Implement**

In `src/modules/graphics/vulkan/Graphics.cpp`, add includes if needed:

```cpp
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include <memory>
```

Implement (place after `newTexture(image::ImageData *)`):

```cpp
Texture *Graphics::newTextureFromFile(const std::string &filename) {
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");

    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fileData(fs->read(filename));
    if (!fileData) throw Exception("newTextureFromFile: failed to read '%s'", filename.c_str());

    auto *imgMod = image::Image::create();
    std::unique_ptr<image::ImageData> data(imgMod->newImageData(fileData.get()));
    return newTexture(data.get());
}
```

Notes:
- `fs->read` throws on missing file (PhysFS open failure) — do not swallow.
- `unique_ptr` deletes temporaries after upload; GPU texture remains in `ownedTextures`.
- If `Exception` printf ctor is unavailable, use string concatenation / `throw Exception("...")` with a fixed message including filename via `std::string`.

- [ ] **Step 4: Link EVFileSystem into eve**

In `src/engine/CMakeLists.txt`, change:

```cmake
set(EVELIBS EVWindow EVMouse EVEvent EVCmdLine EVDevTools EVScripts EVCommon EVGraphics EVImage)
```

to:

```cmake
set(EVELIBS EVWindow EVMouse EVEvent EVCmdLine EVDevTools EVScripts EVCommon EVGraphics EVImage EVFileSystem)
```

(`unit_test` already links `EVFileSystem`.)

- [ ] **Step 5: Build**

Same build command as Task 1. Expected: `unit_test` and (if built) `eve` link successfully.

---

### Task 3: Smoke test — temp PNG write → `newTextureFromFile` → render

**Files:**
- Modify: `test/RenderSystem.cpp`

**Interfaces:**
- Consumes: `Graphics::newTextureFromFile`, `ImageData::encode(FormatHandler::ENCODED_PNG, ...)`, `Filesystem::setIdentity` / `setupWriteDirectory`
- Produces: `GraphicsSmoke` path that proves real PNG decode (plus missing-file throw)

- [ ] **Step 1: Add includes and filesystem bootstrap helper**

At top of `test/RenderSystem.cpp`, add:

```cpp
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"
```

Add helper (near other helpers):

```cpp
static eve::filesystem::Filesystem *bootstrapFilesystemForSaveIO() {
    auto *fs = eve::filesystem::Filesystem::create();
    // Unique-ish identity under user save path; append so write dir is searchable.
    fs->setIdentity("evengine_gfx_smoke", true);
    if (!fs->setupWriteDirectory()) {
        throw std::runtime_error("setupWriteDirectory failed");
    }
    return fs;
}
```

- [ ] **Step 2: Extend / replace smoke body for PNG round-trip**

In `TEST(GraphicsSmoke, clearAndPresentWindow)` (keep window/gfx setup), after graphics init:

1. Call `bootstrapFilesystemForSaveIO()` and `eve::image::Image::create()` (so encoders exist).
2. Build checker pixels into `eve::image::ImageData src(64, 64, "RGBA8");` then `memcpy` pixels.
3. Encode:

```cpp
const char *tmpName = "evengine_smoke_checker.png";
std::unique_ptr<eve::filesystem::FileData> encoded(
    src.encode(medialoader::FormatHandler::ENCODED_PNG, tmpName, true));
ASSERT_NE(encoded, nullptr);
```

4. Load:

```cpp
Texture *fromFile = gfx->newTextureFromFile(tmpName);
ASSERT_NE(fromFile, nullptr);
```

5. Attach `fromFile` to at least one `Renderable2D` sprite (can replace previous raw/`ImageData` texture path or keep one procedural + one from-file — at least one must be from-file).
6. Render ~60 frames as today.
7. Best-effort cleanup: `fs->remove(tmpName)` if API exists (`Filesystem::remove` / delete); ignore failure.

Keep a solid-color sprite so the window still shows non-texture content.

- [ ] **Step 3: Add missing-file test**

```cpp
TEST(GraphicsSmoke, newTextureFromFileThrowsOnMissing) {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    ASSERT_TRUE(win->setWindowSettings(s));

    bootstrapFilesystemForSaveIO();
    EXPECT_THROW(gfx->newTextureFromFile("definitely_missing_evengine_xyz.png"), eve::Exception);

    win->close();
}
```

If constructing a minimal window is too heavy, initialize only what `newTextureFromFile` needs: still require Filesystem bootstrap; Graphics must be `create()`’d — `newTextureFromFile` does not require `initWithWindow` until upload, but `newTexture` **does** require initialized Vulkan. So either:
- keep the small window init as above, **or**
- only `EXPECT_THROW` on read failure **before** upload by using a path that fails in `fs->read` after Filesystem bootstrap and after `gfx->initWithWindow` via a tiny window.

Prefer the small-window version above so the throw path matches production (`read` fails).

- [ ] **Step 4: Run tests**

```powershell
.\build\win32-debug\test\Debug\unit_test.exe --gtest_filter="GraphicsSmoke.*:Batcher.*"
```

Expected:

```
[  PASSED  ] N tests.
```

including `GraphicsSmoke.clearAndPresentWindow` and `GraphicsSmoke.newTextureFromFileThrowsOnMissing`.

- [ ] **Step 5: Regression filter**

```powershell
.\build\win32-debug\test\Debug\unit_test.exe --gtest_filter="GraphicsSmoke.*:Batcher.*:ECS.*"
```

Expected: all PASS.

---

### Task 4: Spec checkbox sync (docs only)

**Files:**
- Modify: `docs/superpowers/specs/2026-08-04-png-texture-from-file-design.md` (section 6 checkboxes → done)
- Optional: one-line note in `docs/2D渲染API设计.md` §6 that `newTextureFromFile` exists

- [ ] **Step 1: Mark acceptance criteria checked** in the spec file after Task 3 passes.
- [ ] **Step 2: Stop** — do not start Quad UV / Camera2D.

---

## Spec Coverage Self-Review

| Spec requirement | Task |
|------------------|------|
| `newTextureFromFile` on Graphics | Task 2 |
| Filesystem → ImageData → newTexture | Task 2 |
| Throw on failure | Task 2 + Task 3 missing-file test |
| Temp PNG encode, not in repo | Task 3 |
| Smoke visible sprite | Task 3 |
| No Quad / Camera2D / Squirrel | Global Constraints + Task 4 stop |
| Link EVFileSystem / EVImage | Task 2 CMake + existing test CMake |
| Encode/decode actually works | Task 1 (prerequisite bugfix) |

## Placeholder / Consistency Scan

- No TBD steps; module name fix called out explicitly.
- API name consistent: `newTextureFromFile(const std::string &filename)`.
- Encoded format constant: `medialoader::FormatHandler::ENCODED_PNG` (avoid unimplemented `ImageData::getConstant`).
