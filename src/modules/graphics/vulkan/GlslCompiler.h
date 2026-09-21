#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {

/** @brief GLSL stage accepted by compileGlslToSpirv. */
enum class GlslStage { eVertex, eFragment, eCompute };

/**
 * @brief Whether this build can compile GLSL text to SPIR-V at runtime.
 *
 * True when the engine was linked against the Vulkan SDK's shaderc archive
 * (Windows builds configured with VULKAN_SDK set) or when an external `glslc`
 * executable is reachable. Callers that can fall back to prebuilt SPIR-V should
 * gate on this instead of probing the platform, so a packaged build without any
 * external tool keeps working.
 *
 * @return True when compileGlslToSpirv can be expected to succeed.
 */
bool glslRuntimeCompilationAvailable();

/**
 * @brief Compile GLSL text to SPIR-V words.
 *
 * Uses the in-process shaderc compiler linked into the engine where available and
 * only spawns the external `glslc` tool for builds that were configured without
 * it. This is the single owner of runtime GLSL -> SPIR-V for every module: the
 * graphics shader factories (`newShader`, `newMeshShader`, ...) and the gpgpu
 * compute path both route through it.
 *
 * @param source GLSL source text; must not be empty.
 * @param stage Stage to compile the source for.
 * @param debugName Virtual file name reported in compiler diagnostics.
 * @return SPIR-V words; the first word is the SPIR-V magic number.
 * @throws std::runtime_error when no compiler is available or compilation fails,
 *         with the compiler diagnostics appended to the message. Deliberately not
 *         eve::Exception: this also runs on CPU worker threads that must not
 *         trigger the global render tracer.
 */
std::vector<std::uint32_t> compileGlslToSpirv(const std::string &source, GlslStage stage,
                                              const std::string &debugName);

}  // namespace eve::graphics
