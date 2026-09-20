#pragma once
#include "common/Result.h"
namespace eve::graphics {
struct VegetationMask;
/** @brief Project a TVE main/detail linear mask into the native PBR ORM channel convention.
 * Source G is occlusion, A is smoothness, B is retained as the authored color mask.
 * Output RGBA is occlusion, roughness, zero metallic, original color mask.
 * This is an owning derived texture; source pixels remain unchanged and authoritative.
 * Factors are [0,1]. Use the source UV transform and sampler for the derived texture.
 * Source pixels must already be linear; this function never applies sRGB conversion.
 * Admits at most 16 million texels, including 4096-square source masks.
 * Worker-safe and reentrant; input is borrowed synchronously without callbacks.
 * @return Complete linear texture or checked validation/allocation failure.
 */
[[nodiscard]] Result<VegetationMask> packVegetationOrm(const VegetationMask& source, float occlusion, float smoothness);
}  // namespace eve::graphics
