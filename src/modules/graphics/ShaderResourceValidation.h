#pragma once
#include "graphics/ShaderResources.h"

namespace eve::graphics::detail {
// Internal admission for set-1 programs, independent of the fixed eve.shader/1 asset ABI.
Result<void> validateResourceShaderStages(std::span<const uint32_t> vertex, std::span<const uint32_t> fragment,
                                          const ShaderResourceInputs& inputs);
}  // namespace eve::graphics::detail
