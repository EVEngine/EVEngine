#pragma once

// Lightweight WebGPU helper types shared by the webgpu backend layer.
// This header only depends on the wgpu C++ wrapper (webgpu_cpp.h); it must not
// include any engine module headers (AGENTS.md layering rules).

#if defined(__EMSCRIPTEN__) && __has_include(<webgpu/webgpu_cpp.h>)
#include <webgpu/webgpu_cpp.h>
#else
#include <dawn/webgpu_cpp.h>
#endif

#include <cstdint>
#include <cstring>
#include <string>

namespace eve::graphics::webgpu {

/** @brief Build a WGPUStringView from a C string (null-safe, length auto-computed). */
inline WGPUStringView sv(const char *s) {
    return WGPUStringView{s, s ? std::strlen(s) : 0};
}

/** @brief Build a WGPUStringView from a std::string. */
inline WGPUStringView sv(const std::string &s) {
    return WGPUStringView{s.data(), s.size()};
}

/** @brief Create an owning shader module; retain it until pipeline creation completes.
 * @param dev Borrowed device used synchronously.
 * @param wgsl Borrowed source; no pointer is retained after this call.
 * @return Reference-counted shader module. Device error callbacks report validation errors.
 */
[[nodiscard]] inline wgpu::ShaderModule makeWgslModule(wgpu::Device &dev, const char *wgsl) {
    wgpu::ShaderSourceWGSL wgslDesc{};
    wgslDesc.code = wgsl;
    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgslDesc;
    return dev.CreateShaderModule(&desc);
}

}  // namespace eve::graphics::webgpu