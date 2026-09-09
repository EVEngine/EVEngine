#pragma once
#include "common/Result.h"

namespace eve::asset {
struct ShaderAsset;
}
namespace eve::asset_graphics {
/**
 * @brief Validate complete SPIR-V semantics and the supported engine pipeline interface before upload.
 * @param shader Borrowed CPU snapshot, not retained.
 * @return Success, a structured incompatibility, or Unsupported when validation tools were not linked.
 * @thread Worker-safe for immutable input. No GPU allocation, callbacks, or live-state mutation.
 * @remarks Version one permits one fragment sampler (albedo), fixed vertex attributes, a mesh Frame UBO,
 * and optional float data[32] push constants. Other descriptor layouts are explicitly unsupported.
 */
[[nodiscard]] Result<void> validateShaderAssetGpu(const asset::ShaderAsset& shader);
}  // namespace eve::asset_graphics
