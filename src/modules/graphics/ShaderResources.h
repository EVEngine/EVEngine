#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "common/Result.h"
#include "graphics/TextureSampler.h"

namespace eve::graphics {

/** @brief Explicit sampled-image storage; sRGB conversion never affects alpha. */
enum class ShaderImageFormat {
    R8,
    RG8,
    R16,
    RGBA8,
    BGRA8,
    RGBA8Srgb,
    BGRA8Srgb,
    BC1,
    BC1Srgb,
    BC3,
    BC3Srgb,
    BC7,
    BC7Srgb
};
/** @brief Sampled image view dimension; arrays retain all authored layers. */
enum class ShaderImageDimension { Image2D, Array2D, Cube };

/**
 * @brief Synchronous borrowed input for one immutable shader image.
 * @ownership The caller owns bytes; upload copies them before returning.
 * @lifetime All spans need remain valid only for the upload call; never retained.
 * @details Bytes are tightly packed layer-major, then mip-major. BC3 tail mips
 * occupy at least one 4x4 block. Numeric images are never premultiplied/resampled.
 */
struct ShaderImageInput {
    std::uint32_t              binding   = 0;
    ShaderImageFormat          format    = ShaderImageFormat::RGBA8;
    ShaderImageDimension       dimension = ShaderImageDimension::Image2D;
    std::uint32_t              width = 0, height = 0, layers = 1, mipLevels = 1;
    TextureSampler             sampler;
    std::span<const std::byte> bytes;
    /** @brief Optional immutable byte snapshot identity for upload deduplication.
     * @ownership Shared owner of the exact bytes above, held only for the call.
     * Never reuse an owner for mutated bytes. Empty disables identity reuse.
     */
    std::shared_ptr<const void> contentOwner;
};

/**
 * @brief Inputs for descriptor set 1 of a resource mesh program.
 * @details Image bindings are unique in [0,31]; optional std140 uniform bytes
 * use binding 32. Images/constant bytes become immutable GPU-owned copies.
 * @ownership The caller owns every input span through the synchronous call.
 */
struct ShaderResourceInputs {
    std::span<const ShaderImageInput> images;
    std::span<const std::byte>        constants;
    /** @brief Optional immutable column-major float32 affine mat4 records, set 1 binding 33.
     * @ownership Caller bytes are borrowed only during synchronous upload and copied.
     * @details Vertex-only readonly std430 storage; finite invertible transforms,
     * at most 64 MiB. Empty preserves the ordinary non-instanced program ABI.
     */
    std::span<const std::byte> instanceMatrices;
};

/** @brief Validate packed instance records without GPU access; returns the record count.
 * @param bytes Borrowed unaligned bytes, retained only for this call. Worker-safe.
 * @return Count or diagnostic; empty input is valid and returns zero.
 */
[[nodiscard]] Result<std::uint32_t> shaderInstanceMatrixCount(std::span<const std::byte> bytes);

/** @brief One validated byte region for an image's layer/mip upload. */
struct ShaderImageRegion {
    std::uint32_t layer = 0, mip = 0, width = 0, height = 0;
    std::size_t   offset = 0, size = 0;
};

/**
 * @brief Validate layout, dimensions, sampler compatibility and exact payload size.
 * @param image Borrowed input, read only during this call; may be validated on workers.
 * @return Owning region metadata, or a diagnostic. No GPU state is touched.
 */
[[nodiscard]] Result<std::vector<ShaderImageRegion>> shaderImageRegions(const ShaderImageInput& image);

}  // namespace eve::graphics
