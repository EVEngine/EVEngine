#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "common/Result.h"

namespace eve::gpgpu {
class ComputeShader;
/** @brief Compile GLSL compute source to owning SPIR-V without accessing Graphics or a GPU.
 * @param source Borrowed until return; independent calls may execute concurrently on worker threads.
 * @return Owning words, compilation diagnostic, or Unsupported on WebGPU. No callbacks; each thread retains its own
 * compiler context until thread exit.
 */
[[nodiscard]] Result<std::vector<uint32_t>> compileComputeSpirv(const std::string& source);
/** @brief Create an owning compute pipeline from valid SPIR-V produced by compileComputeSpirv.
 * @param words Borrowed only until return; must contain a valid compute entry point named main.
 * @return Owning shader or a diagnostic; Unsupported on WebGPU.
 * @note Device thread only, no concurrent calls/reentrancy or Graphics retirement during the call.
 * The caller must destroy the shader before the active Graphics device retires.
 */
[[nodiscard]] Result<std::unique_ptr<ComputeShader>> createComputeShader(const std::vector<uint32_t>& words);
}  // namespace eve::gpgpu
