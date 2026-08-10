# GPGPU Backend Abstraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `ComputeShader` / `GpuBuffer` / `Gpgpu` backend-agnostic via abstract base classes, with Vulkan details living only under `gpgpu/vulkan/`.

**Architecture:** Public types become abstract interfaces. `Gpgpu` factories return base pointers to `VulkanComputeShader` / `VulkanGpuBuffer`. Backend selection follows `Graphics::getBackendName()` (`"vulkan"` today). SPIR-V path remains as a compatibility wrapper over a generic bytecode loader.

**Tech Stack:** C++20, Vulkan / vkbuilder, ZeroErr unit tests, Squirrel bindings via simplesquirrel, CMake `GLOB_RECURSE` module sources.

**Spec:** [docs/dev/superpowers/specs/2026-08-10-gpgpu-backend-abstraction-design.md](../specs/2026-08-10-gpgpu-backend-abstraction-design.md)

## Global Constraints

- Structure mode is **abstract base + Vulkan derived** (not PIMPL facade).
- Public headers `ComputeShader.h` / `GpuBuffer.h` / `Gpgpu.h` must **not** include `vkbuilder.hpp` or any Vulkan types.
- Backend auto-matches Graphics; no independent GPGPU backend registry.
- Keep sync `dispatch` semantics; do not add async queues.
- Keep binding / float out-of-range silent ignore behavior.
- Preserve script API surface; add `newShaderFromBytecode`; keep `newShaderFromSpvFile` as wrapper.
- Do **not** create git commits unless the user explicitly asks (repo commit rule overrides “frequent commits” in the skill template).
- Prefer Linux WSL / existing `build/linux-debug` when available; on Windows use Vulkan SDK PATH as in other plans.

## File Structure

| File | Role |
|------|------|
| `src/modules/graphics/Graphics.h` | Add `getBackendName()` |
| `src/modules/graphics/vulkan/Graphics.h` / `.cpp` | Override → `"vulkan"` |
| `src/modules/gpgpu/ComputeShader.h` | Abstract base only |
| `src/modules/gpgpu/ComputeShader.cpp` | Shared push helpers (or empty if all pure virtual in derived) |
| `src/modules/gpgpu/GpuBuffer.h` | Abstract base only |
| `src/modules/gpgpu/GpuBuffer.cpp` | Remove / empty (logic moves to Vulkan) |
| `src/modules/gpgpu/Gpgpu.h` / `.cpp` | Factories + dispatch routing + script expose |
| `src/modules/gpgpu/vulkan/VulkanUtil.h` / `.cpp` | Move from `gpgpu/VulkanUtil.*` |
| `src/modules/gpgpu/vulkan/VulkanComputeShader.h` / `.cpp` | Vulkan pipeline + descriptors |
| `src/modules/gpgpu/vulkan/VulkanGpuBuffer.h` / `.cpp` | Vulkan buffer + transfers |
| `src/modules/gpgpu/vulkan/VulkanGpgpu.cpp` | Vulkan create / dispatch helpers used by `Gpgpu.cpp` |
| `test/gpgpu.cpp` | Cover backend name + bytecode alias; keep scaleFloats |
| `docs/usr/modules/gpgpu.md` | Document bytecode API |

CMake: `create_module(EVGpgpu gpgpu)` already uses recursive `*.cpp` glob — new `gpgpu/vulkan/*.cpp` are picked up automatically after a reconfigure/rescan.

---

### Task 1: `Graphics::getBackendName`

**Files:**
- Modify: `src/modules/graphics/Graphics.h`
- Modify: `src/modules/graphics/vulkan/Graphics.h`
- Modify: `src/modules/graphics/vulkan/Graphics.cpp`
- Test: `test/gpgpu.cpp` (add case in this task or Task 5)

**Interfaces:**
- Consumes: existing `eve::graphics::Graphics` / `vulkan::Graphics`
- Produces: `virtual std::string getBackendName() const` on base; Vulkan returns `"vulkan"`

- [ ] **Step 1: Declare on public Graphics**

In `src/modules/graphics/Graphics.h`, inside `class Graphics`, add near other queries:

```cpp
/** Renderer backend id used by sibling modules (e.g. Gpgpu). */
virtual std::string getBackendName() const = 0;
```

Ensure `#include <string>` remains present (already there).

- [ ] **Step 2: Override in Vulkan Graphics**

In `src/modules/graphics/vulkan/Graphics.h`:

```cpp
std::string getBackendName() const override;
```

In `src/modules/graphics/vulkan/Graphics.cpp`:

```cpp
std::string Graphics::getBackendName() const { return "vulkan"; }
```

- [ ] **Step 3: Build Graphics / unit_test target**

```powershell
# Linux WSL example:
cmake --build build/linux-debug --target unit_test -j 8
```

Expected: compile succeeds (pure virtual must be overridden).

---

### Task 2: Turn public `ComputeShader` / `GpuBuffer` into abstract bases

**Files:**
- Rewrite: `src/modules/gpgpu/ComputeShader.h`
- Rewrite: `src/modules/gpgpu/GpuBuffer.h`
- Modify or slim: `src/modules/gpgpu/ComputeShader.cpp`, `src/modules/gpgpu/GpuBuffer.cpp`
- Note: temporary build break until Task 3/4 land — keep this task + Task 3 in one agent session if preferred

**Interfaces:**
- Produces: abstract APIs matching the spec (no Vulkan includes)

- [ ] **Step 1: Replace `ComputeShader.h`**

```cpp
#pragma once

#include <array>
#include <cstdint>

namespace eve::gpgpu {

class GpuBuffer;

/**
 * Backend-agnostic compute program.
 * Bind storage buffers then dispatch via Gpgpu::dispatch.
 * Push constants: float[32] (same size as graphics::Shader).
 */
class ComputeShader {
public:
    static constexpr int kMaxBindings = 8;
    static constexpr int kMaxFloats = 32;
    static constexpr uint32_t kPushConstantBytes = uint32_t(kMaxFloats * sizeof(float));

    ComputeShader() = default;
    virtual ~ComputeShader() = default;

    ComputeShader(const ComputeShader &) = delete;
    ComputeShader &operator=(const ComputeShader &) = delete;

    /** Bind a storage buffer to set=0 binding. binding in [0, kMaxBindings). */
    virtual void bindBuffer(int binding, GpuBuffer *buffer) = 0;
    virtual GpuBuffer *getBoundBuffer(int binding) const = 0;

    virtual void setFloat(int index, float value) = 0;
    virtual float getFloat(int index) const = 0;

    virtual void clearBindings() = 0;

protected:
    std::array<float, kMaxFloats> push_{};
};

}  // namespace eve::gpgpu
```

- [ ] **Step 2: Replace `GpuBuffer.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace eve::data {
class ByteData;
}

namespace eve::gpgpu {

/**
 * Backend-agnostic GPU buffer for compute (storage) or CPU staging transfers.
 * Squirrel-owned; derived class destroys GPU resources in destructor.
 */
class GpuBuffer {
public:
    GpuBuffer() = default;
    virtual ~GpuBuffer() = default;

    GpuBuffer(const GpuBuffer &) = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;

    virtual int getSize() const = 0;
    virtual std::string getUsage() const = 0;

    virtual void writeData(data::ByteData *data, int dstOffset = 0) = 0;
    virtual data::ByteData *readData(int srcOffset = 0, int size = -1) = 0;

    virtual void writeFloat32(int floatIndex, float value) = 0;
    virtual float readFloat32(int floatIndex) = 0;
    virtual void fillFloat32(float value) = 0;

    virtual void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) = 0;
    virtual void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) = 0;
};

}  // namespace eve::gpgpu
```

- [ ] **Step 3: Slim `.cpp` files**

Temporarily leave `ComputeShader.cpp` / `GpuBuffer.cpp` as empty namespace stubs **or** delete their bodies and keep empty files so CMake still lists them. Do not leave Vulkan symbols in these TUs.

Example empty stub:

```cpp
#include "gpgpu/ComputeShader.h"

namespace eve::gpgpu {
}  // namespace eve::gpgpu
```

Same for `GpuBuffer.cpp`.

---

### Task 3: Add `gpgpu/vulkan` implementations

**Files:**
- Create: `src/modules/gpgpu/vulkan/VulkanUtil.h`
- Create: `src/modules/gpgpu/vulkan/VulkanUtil.cpp` (move from old `VulkanUtil.*`)
- Delete: `src/modules/gpgpu/VulkanUtil.h`, `src/modules/gpgpu/VulkanUtil.cpp`
- Create: `src/modules/gpgpu/vulkan/VulkanComputeShader.h` / `.cpp`
- Create: `src/modules/gpgpu/vulkan/VulkanGpuBuffer.h` / `.cpp`
- Create: `src/modules/gpgpu/vulkan/VulkanGpgpu.h` / `.cpp`

**Interfaces:**
- Consumes: abstract `ComputeShader` / `GpuBuffer`; `graphics::vulkan::Graphics`
- Produces:
  - `class VulkanComputeShader : public ComputeShader`
  - `class VulkanGpuBuffer : public GpuBuffer`
  - `namespace vulkan_detail` helpers: `buildComputeShader(spv)`, `buildBuffer(...)`, `dispatch(...)`, `isVulkanGpgpuReady()`

- [ ] **Step 1: Move VulkanUtil**

Move content of `gpgpu/VulkanUtil.h` → `gpgpu/vulkan/VulkanUtil.h` with include path update:

```cpp
#include "graphics/vulkan/Graphics.h"
```

Move `.cpp` similarly; change include to `"gpgpu/vulkan/VulkanUtil.h"`.

Update any remaining includes from `"gpgpu/VulkanUtil.h"` to `"gpgpu/vulkan/VulkanUtil.h"`.

- [ ] **Step 2: Implement `VulkanGpuBuffer`**

`VulkanGpuBuffer.h`:

```cpp
#pragma once
#include "gpgpu/GpuBuffer.h"
#include "vkbuilder.hpp"
#include <string>

namespace eve::gpgpu {

class VulkanGpuBuffer final : public GpuBuffer {
public:
    ~VulkanGpuBuffer() override;

    int getSize() const override { return int(size_); }
    std::string getUsage() const override { return usage_; }

    void writeData(data::ByteData *data, int dstOffset = 0) override;
    data::ByteData *readData(int srcOffset = 0, int size = -1) override;
    void writeFloat32(int floatIndex, float value) override;
    float readFloat32(int floatIndex) override;
    void fillFloat32(float value) override;
    void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) override;
    void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) override;

    vkb::Device *device_ = nullptr;
    vk::Buffer buffer_{};
    vk::DeviceMemory memory_{};
    vk::DeviceSize size_ = 0;
    std::string usage_ = "storage";
    bool hostVisible_ = false;
};

}  // namespace eve::gpgpu
```

Move the current `GpuBuffer.cpp` method bodies into `VulkanGpuBuffer.cpp`, adjusting class names and includes (`VulkanUtil`, `ByteData`, etc.).

- [ ] **Step 3: Implement `VulkanComputeShader`**

`VulkanComputeShader.h`:

```cpp
#pragma once
#include "gpgpu/ComputeShader.h"
#include "vkbuilder.hpp"
#include <array>

namespace eve::gpgpu {

class VulkanComputeShader final : public ComputeShader {
public:
    ~VulkanComputeShader() override;

    void bindBuffer(int binding, GpuBuffer *buffer) override;
    GpuBuffer *getBoundBuffer(int binding) const override;
    void setFloat(int index, float value) override;
    float getFloat(int index) const override;
    void clearBindings() override;

    void flushDescriptors(vkb::Device &device);

    vkb::Device *device_ = nullptr;
    vk::ShaderModule module_{};
    vk::DescriptorSetLayout setLayout_{};
    vk::PipelineLayout pipelineLayout_{};
    vk::Pipeline pipeline_{};
    vk::DescriptorPool descriptorPool_{};
    vk::DescriptorSet descriptorSet_{};
    vk::Buffer dummyBuffer_{};
    vk::DeviceMemory dummyMemory_{};

    std::array<GpuBuffer *, kMaxBindings> bindings_{};
    bool descriptorsDirty_ = true;
};

}  // namespace eve::gpgpu
```

Move current `ComputeShader.cpp` logic into `VulkanComputeShader.cpp`.

`setFloat` / `getFloat` should use protected `push_`:

```cpp
void VulkanComputeShader::setFloat(int index, float value) {
    if (index < 0 || index >= kMaxFloats) return;
    push_[size_t(index)] = value;
}
float VulkanComputeShader::getFloat(int index) const {
    if (index < 0 || index >= kMaxFloats) return 0.f;
    return push_[size_t(index)];
}
```

In `flushDescriptors`, cast bound buffers:

```cpp
auto *vb = dynamic_cast<VulkanGpuBuffer *>(bindings_[size_t(i)]);
infos[size_t(i)].buffer = (vb && vb->buffer_) ? vb->buffer_ : dummyBuffer_;
infos[size_t(i)].range = (vb && vb->buffer_) ? vb->size_ : vk::DeviceSize(4);
```

Include `"gpgpu/vulkan/VulkanGpuBuffer.h"`.

- [ ] **Step 4: Add `VulkanGpgpu` helpers**

`VulkanGpgpu.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

bool vulkanGpgpuReady();
ComputeShader *vulkanNewShaderFromSpirv(const std::vector<uint32_t> &spv);
GpuBuffer *vulkanNewBuffer(int byteSize, const std::string &usage);
void vulkanDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ);

}  // namespace eve::gpgpu
```

Move the bodies of current anonymous `buildComputeShader` / `buildBuffer` / `Gpgpu::dispatch` Vulkan recording from `Gpgpu.cpp` into `VulkanGpgpu.cpp`, constructing `VulkanComputeShader` / `VulkanGpuBuffer` instead of the old concrete types.

`vulkanDispatch` must:

```cpp
auto *vs = dynamic_cast<VulkanComputeShader *>(shader);
if (!vs || !vs->pipeline_) return;
// ... existing executeImmediately path using vs->pipeline_, vs->pipelineLayout_,
// vs->descriptorSet_, vs->push_.data(), vs->flushDescriptors(device)
```

- [ ] **Step 5: Reconfigure sources if needed**

If the build system caches module source lists, touch/rescan so `gpgpu/vulkan/*.cpp` appear. Then build `EVGpgpu`.

Expected: `EVGpgpu` compiles (may still fail link of `Gpgpu.cpp` until Task 4).

---

### Task 4: Rewire `Gpgpu` module facade

**Files:**
- Modify: `src/modules/gpgpu/Gpgpu.h`
- Modify: `src/modules/gpgpu/Gpgpu.cpp`

**Interfaces:**
- Consumes: `Graphics::getBackendName()`, `vulkanNew*` helpers
- Produces: public factories returning base pointers; `newShaderFromBytecode`; Spv wrapper

- [ ] **Step 1: Update `Gpgpu.h`**

```cpp
ComputeShader *newShader(const std::string &source);
ComputeShader *newShaderFromBytecode(const std::string &path);
ComputeShader *newShaderFromSpvFile(const std::string &path);
GpuBuffer *newBuffer(int byteSize, const std::string &usage = "storage");
void dispatch(ComputeShader *shader, int groupsX, int groupsY = 1, int groupsZ = 1);
```

Update the class comment: no longer “Vulkan compute” only — “GPU compute via the active Graphics backend”.

- [ ] **Step 2: Implement routing in `Gpgpu.cpp`**

```cpp
#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/vulkan/VulkanGpgpu.h"
#include "gpgpu/vulkan/VulkanUtil.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "graphics/Graphics.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::gpgpu {
namespace {

std::string currentGraphicsBackend() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    if (!gfx) return {};
    return gfx->getBackendName();
}

}  // namespace

Module_IMPL(Gpgpu, new Gpgpu());

bool Gpgpu::isAvailable() const {
    if (currentGraphicsBackend() != "vulkan") return false;
    return vulkanGpgpuReady();
}

ComputeShader *Gpgpu::newShader(const std::string &source) {
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShader: requires vulkan Graphics backend");
    return vulkanNewShaderFromSpirv(compileComputeGlsl(source));
}

ComputeShader *Gpgpu::newShaderFromBytecode(const std::string &path) {
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShaderFromBytecode: requires vulkan Graphics backend");
    return vulkanNewShaderFromSpirv(loadSpirvFile(path));
}

ComputeShader *Gpgpu::newShaderFromSpvFile(const std::string &path) {
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShaderFromSpvFile: SPIR-V is only supported on vulkan");
    return newShaderFromBytecode(path);
}

GpuBuffer *Gpgpu::newBuffer(int byteSize, const std::string &usage) {
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newBuffer: requires vulkan Graphics backend");
    return vulkanNewBuffer(byteSize, usage);
}

void Gpgpu::dispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ) {
    if (!shader) return;
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.dispatch: requires vulkan Graphics backend");
    vulkanDispatch(shader, groupsX, groupsY, groupsZ);
}
```

Keep `expose` bindings on base methods; add:

```cpp
cls.addFunc("newShaderFromBytecode", &Gpgpu::newShaderFromBytecode);
```

Remove old inline Vulkan create/dispatch from `Gpgpu.cpp` (now in `VulkanGpgpu.cpp`).

- [ ] **Step 3: Build `EVGpgpu` + `unit_test`**

Expected: full link succeeds. Fix any leftover includes of deleted `gpgpu/VulkanUtil.h` or old public Vulkan members (`shader->pipeline_`, `buf->buffer_`) — `tensor/GpuBackend.cpp` should already use only public methods; verify with grep:

```powershell
rg "pipeline_|flushDescriptors|vkbuilder|VulkanUtil" src/modules/tensor src/modules/gpgpu test
```

Expected: Vulkan symbols only under `src/modules/gpgpu/vulkan/`.

---

### Task 5: Tests + user docs

**Files:**
- Modify: `test/gpgpu.cpp`
- Modify: `docs/usr/modules/gpgpu.md`

**Interfaces:**
- Consumes: `Graphics::getBackendName`, `Gpgpu::newShaderFromBytecode`, abstract buffer/shader APIs

- [ ] **Step 1: Add backend-name test**

```cpp
TEST_CASE("gpgpu.graphics.backendName") {
    if (!tryInitGpuWindow()) return;
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    CHECK_EQ(gfx->getBackendName(), std::string("vulkan"));
}
```

- [ ] **Step 2: Keep `gpgpu.dispatch.scaleFloats`; optionally assert bytecode path exists**

If no checked-in `.spv` asset, do **not** add a fragile filesystem dependency. Instead add a lightweight check that the method is callable when unavailable bytecode would throw — or skip. Prefer documenting that `newShaderFromSpvFile` delegates to `newShaderFromBytecode` via a unit-test-free code review of `Gpgpu.cpp`, **or** add:

```cpp
TEST_CASE("gpgpu.newShaderFromSpvFile.delegatesWhenMissing") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Gpgpu::create();
    if (!mod->isAvailable()) return;
    bool threw = false;
    try {
        mod->newShaderFromSpvFile("__eve_missing_compute.spv");
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}
```

- [ ] **Step 3: Update `docs/usr/modules/gpgpu.md`**

- Soften “Vulkan-only” wording to “follows Graphics backend (currently Vulkan)”.
- Add `newShaderFromBytecode()` to API list.
- Note `newShaderFromSpvFile` is Vulkan SPIR-V compatibility wrapper.

- [ ] **Step 4: Run gpgpu tests**

```powershell
# After building unit_test:
./build/linux-debug/unit_test --filter gpgpu
# or whatever ZeroErr filter flag this repo uses; if none, run unit_test and visually confirm gpgpu.* cases
```

Expected: `gpgpu.module.create` PASS; GPU cases PASS or skip cleanly when window/glslc missing (same tolerant pattern as today).

---

### Task 6: Regression check for tensor GPU path

**Files:**
- Verify only: `src/modules/tensor/GpuBackend.cpp` (no Vulkan member access)
- Test: `test/tensor.cpp` GPU-tolerant cases

- [ ] **Step 1: Grep tensor for Vulkan leakage**

```powershell
rg "vk::|vkbuilder|buffer_|pipeline_|device_" src/modules/tensor/GpuBackend.cpp
```

Expected: no `vk::` / `vkbuilder`; only `gpgpu::` base APIs and local variables named `device_` on tensor types (unrelated).

- [ ] **Step 2: Build and run tensor tests**

```powershell
cmake --build build/linux-debug --target unit_test -j 8
./build/linux-debug/unit_test --filter tensor
```

Expected: CPU tensor tests PASS; GPU cases skip or PASS as before.

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| Abstract `ComputeShader` / `GpuBuffer` | Task 2 |
| Vulkan derived under `gpgpu/vulkan/` | Task 3 |
| Public headers free of Vulkan | Task 2–4 |
| `Graphics::getBackendName` + auto match | Task 1, 4 |
| `newShaderFromBytecode` + Spv wrapper | Task 4, 5 |
| Sync dispatch + error table | Task 3–4 |
| Script bind base classes + new method | Task 4 |
| tensor still works via base API | Task 6 |
| No second backend / no async / no registry | (out of scope — not tasked) |

## Self-review notes

- No TBD placeholders.
- Types consistent: `VulkanComputeShader` / `VulkanGpuBuffer` / `vulkanNew*` / `vulkanDispatch` / `vulkanGpgpuReady`.
- Commit steps omitted intentionally per repo rule; ask user before committing.
