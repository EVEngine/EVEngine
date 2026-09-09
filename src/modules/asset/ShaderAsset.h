#pragma once

#include "common/ResourceRef.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::asset {

class EvpackResourceReader;
struct EvpackCapabilities;

/** @brief Fixed graphics binding contract; this is not an arbitrary pipeline layout. */
enum class ShaderAssetInterface { Sprite2D, Mesh3D };

/** @brief One sequential float/vector push-constant parameter, owned by the definition. */
struct ShaderAssetParameter {
    std::string        name;
    std::vector<float> defaults;
};

/** @brief Owning CPU program snapshot; decoding does not prove GPU/descriptor compatibility. */
struct ShaderAsset {
    ShaderAssetInterface              interface = ShaderAssetInterface::Mesh3D;
    std::vector<std::uint32_t>        vertex;
    std::vector<std::uint32_t>        fragment;
    std::vector<ShaderAssetParameter> parameters;
};

/** @brief Admission bounds checked before copying stage words. */
struct ShaderAssetLimits {
    std::uint32_t maximumStageWords = 262144;
};

/**
 * @brief Decode strict `eve.shader/1` metadata with SPIR-V 1.0 through 1.6 stages.
 * @param definition Borrowed owning tree, retained only during this call.
 * @param limits Per-stage allocation budget.
 * @return Owning CPU snapshot or diagnostic; unknown fields/versions are rejected.
 * @remarks Validates framing and main entry-point stages, not full SPIR-V semantics or reflection.
 * Parameters occupy consecutive float slots, at most 32, with one to four finite components each.
 * No version zero format exists; future versions require explicit migration.
 * @thread Worker-safe with immutable input. No callbacks, GPU objects, or live-state mutation.
 */
[[nodiscard]] Result<ShaderAsset> decodeShaderAsset(const Value& definition, const ShaderAssetLimits& limits = {});

/**
 * @brief Read and decode one capability-selected `eve.shader/1` from an admitted runtime package.
 * @param reader Borrowed for this call; the result owns all decoded data.
 * @param asset Persistent shader identity, not a runtime GPU handle.
 * @param capabilities Actual device capabilities; only Vulkan SPIR-V is supported in version one.
 * @return Independent CPU candidate or a diagnostic; no live state is changed.
 * @thread Worker-safe. No callbacks or GPU allocation. GPU validation remains the upload boundary's responsibility.
 */
[[nodiscard]] Result<ShaderAsset> loadShaderAsset(const EvpackResourceReader& reader, const AssetRef& asset,
                                                  const EvpackCapabilities& capabilities);

}  // namespace eve::asset
