#pragma once

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
 * @brief Validate `eve.image/2` JSON and decode its PNG source into bounded EVIMG RGBA8.
 * @param definition Canonical source definition bytes.
 * @param encodedSource PNG bytes referenced by the definition.
 * @param maximumDecodedBytes Upper bound including the EVIMG header and pixels.
 * @return Owning runtime candidate; JPEG and unsupported PNG modes fail explicitly.
 * @thread Worker-safe; uses no global decoder state.
 */
[[nodiscard]] Result<CookedCanonicalImage> cookCanonicalImageRgba8(
    std::span<const std::uint8_t> definition,
    std::span<const std::uint8_t> encodedSource,
    std::uint64_t maximumDecodedBytes);

}  // namespace eve::asset
