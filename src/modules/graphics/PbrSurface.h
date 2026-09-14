#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "common/Result.h"
namespace eve::graphics {
class Texture;
/** @brief Canonical glTF texture roles, with fixed semantic channels (not a UV set limit). */
enum class PbrTextureSlot : uint32_t {
    BaseColor,
    MetallicRoughness,
    Normal,
    Occlusion,
    Emissive,
    Specular,
    SpecularColor,
    Anisotropy,
    Clearcoat,
    ClearcoatRoughness,
    ClearcoatNormal,
    Count
};
/** @brief One borrowed texture plus independently owned UV/sampler parameters.
 * Texture is graphics-factory-owned and must outlive this binding and queued draws.
 */
struct PbrTextureBinding {
    Texture*             texture    = nullptr;
    uint32_t             texcoord   = 0;
    bool                 srgbDecode = true;
    std::array<float, 2> offset{0, 0}, scale{1, 1};
    float                rotation = 0;
    uint32_t             wrapS = 10497, wrapT = 10497, minFilter = 9987, magFilter = 9729;
};
/** @brief Owning material parameter snapshot; texture pointers remain borrowed.
 * Base color/metallic/roughness continue to be owned by Material's existing fields.
 * This value owns extension parameters and all eleven texture bindings.
 */
struct PbrSurface {
    std::array<PbrTextureBinding, std::size_t(PbrTextureSlot::Count)> textures{};
    std::array<float, 3>                                              emissive{0, 0, 0}, specularColor{1, 1, 1};
    float normalScale = 1, occlusionStrength = 1, emissiveStrength = 1;
    float specularFactor = 1, ior = 1.5f;
    float anisotropyStrength = 0, anisotropyRotation = 0;
    float clearcoatFactor = 0, clearcoatRoughness = 0, clearcoatNormalScale = 1;
    bool  unlit = false;
};
/** @brief Validate finite material factors and sampler enums without changing the input.
 * @return Checked validation status; performs no IO, backend calls or callbacks.
 * @thread Worker-safe and reentrant. Texture pointers are never dereferenced or retained.
 */
[[nodiscard]] Result<void> validatePbrSurface(const PbrSurface& surface);
}  // namespace eve::graphics
