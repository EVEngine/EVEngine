#pragma once
#include <memory>
#include <cstdint>
#include "common/GpuResidentBufferView.h"
#include "common/Export.h"
#include "common/Result.h"

namespace eve::graphics {
class Graphics;
class Shader;
struct ShaderImageInput;
/**
 * @brief Owning prepared replacement bytes for one existing, unshared resource-program image.
 * @ownership Owns a copied payload; retains no provider, shader, input span or callback.
 * @thread Preparation is worker-safe. Application is graphics-owner-thread-only, non-reentrant.
 * Const tokens may be reused; destruction/moves require exclusive access.
 */
class EVENGINE_API_BACKENDS ShaderImageUpdate {
public:
    /** @brief Release the owned payload. */
    ~ShaderImageUpdate();
    /** @brief Transfer the payload. */
    ShaderImageUpdate(ShaderImageUpdate&&) noexcept;
    /** @brief Replace the payload by ownership transfer. */
    ShaderImageUpdate& operator=(ShaderImageUpdate&&) noexcept;
    ShaderImageUpdate(const ShaderImageUpdate&)            = delete;
    ShaderImageUpdate& operator=(const ShaderImageUpdate&) = delete;
    /**
     * @brief Validate layout and copy one complete image update without GPU access.
     * @param image Borrowed bytes and shape; no input reference is retained. Deduplication identity must be empty.
     * @return Owning token or a layout/allocation failure, without observable GPU changes.
     * @cost Linear payload validation/copy and owned storage per update; prepare off the render thread if useful.
     */
    [[nodiscard]] static Result<ShaderImageUpdate> prepare(const ShaderImageInput& image);
    /**
     * @brief Replace bytes without changing the target image shape, descriptors, buffers or pipelines.
     * @param graphics Borrowed live owner for this call only.
     * @param shader Borrowed live shader owned by graphics, not retained.
     * @return Success or structured failure. Unsupported providers, shape mismatch and shared/deduplicated
     * images reject before writes. Device/queue failure after submission requires host device recovery;
     * no rollback of submitted GPU writes is promised.
     * @remarks Outside submission only. Waits for prior graphics resource use before writing. No callbacks.
     * Existing immutable replacement and sharing paths are unchanged unless this operation is explicitly called.
     * @cost Linear staging copy/upload plus a scan of this provider's shaders and a GPU synchronization
     * per application, with no pipeline compilation. Batch updates rather than invoking once per object.
     */
    [[nodiscard]] Result<void> apply(Graphics& graphics, Shader& shader) const;

private:
    struct Impl;
    explicit ShaderImageUpdate(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Prepared shape for copying a same-device RGBA32F resident buffer into one mesh shader image.
 * @ownership Owns only dimensions and binding; retains no shader, provider or native buffer handle.
 * @thread Preparation is worker-safe; apply is graphics-owner-thread-only and non-reentrant.
 * @lifetime Source buffer and provider must remain live through the synchronous apply call.
 */
class EVENGINE_API_BACKENDS ResidentShaderImageUpdate {
public:
    /** @brief Validate a single-mip 2D RGBA32F image extent and binding without GPU work.
     * @return Prepared shape or structured input failure.
     */
    [[nodiscard]] static Result<ResidentShaderImageUpdate> prepare(std::uint32_t binding, std::uint32_t width,
                                                                   std::uint32_t height);
    /** @brief Copy a completed same-device buffer slice into the existing uniquely owned shader image.
     * @param graphics Borrowed live provider; no reference is retained.
     * @param shader Borrowed resource shader owned by graphics; no reference is retained.
     * @param source Borrowed transient buffer view with at least width*height*16 bytes from offset.
     * @return Success after visibility to later draws, or structured backend/shape/ownership failure.
     * @cost O(width*height) device transfer plus graphics synchronization; amortize preparation and
     * reuse the same image and pipeline across frames.
     */
    [[nodiscard]] Result<void> apply(Graphics& graphics, Shader& shader, const GpuResidentBufferView& source) const;

private:
    ResidentShaderImageUpdate(std::uint32_t binding, std::uint32_t width, std::uint32_t height)
        : binding_(binding), width_(width), height_(height) {}
    std::uint32_t binding_ = 0, width_ = 0, height_ = 0;
};
}  // namespace eve::graphics
