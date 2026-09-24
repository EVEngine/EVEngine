#pragma once
#include "common/Export.h"


/** @file CanonicalImageCook.h @brief Safe source-image conversion to runtime EVIMG payloads. */

#include "common/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eve::asset {

/** @brief Runtime definition and typed RGBA8 bulk generated from one canonical image source. */
struct CookedCanonicalImage {
    std::vector<std::uint8_t> definition;
    std::vector<std::uint8_t> bulk;
};

/**
 * @brief Decode image/2 base images or image/3 explicit linear RGBA8 mip chains into bounded EVIMG.
 * @param
 * definition Canonical source definition bytes.
 * @param encodedSource Encoded image bytes referenced by the definition.
 * @param maximumDecodedBytes Upper bound
 * including the EVIMG header and pixels.
 * @return Owning runtime candidate; JPEG and unsupported PNG modes fail explicitly.
 * @details Image/3 requires
 * mipCount; rgba8-mips is top-down level-major RGBA8, with a base-only
 * or complete halving chain. Image/2 retains
 * EVIMG v1; image/3 emits EVIMG v2 with mip count.
 * @thread Worker-safe; uses no global decoder state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<CookedCanonicalImage> cookCanonicalImageRgba8(
    std::span<const std::uint8_t> definition, std::span<const std::uint8_t> encodedSource,
    std::uint64_t maximumDecodedBytes);

}  // namespace eve::asset
