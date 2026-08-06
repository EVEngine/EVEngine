#pragma once

#include "common/Module.h"

#include <string>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

/**
 * GPGPU module — Vulkan compute shaders + storage buffers.
 * Uses the graphics queue (Apple/MoltenVK has no separate compute family).
 *
 * Script: `gpgpu <- eve.Gpgpu(); shader <- gpgpu.newShader(glsl);`
 */
class Gpgpu : public Module {
public:
    Module_REG(Gpgpu);
    Gpgpu() = default;
    ~Gpgpu() override = default;

    /** True when Vulkan Graphics has a live device (after window init). */
    bool isAvailable() const;

    /** Compile GLSL compute (#version 450) via glslc. Returns nullptr / throws on failure. */
    ComputeShader *newShader(const std::string &glsl);

    /** Load precompiled SPIR-V compute module from Filesystem path. */
    ComputeShader *newShaderFromSpvFile(const std::string &path);

    /**
     * Allocate a GPU buffer.
     * usage: "storage" (SSBO, device-local) | "staging" (host-visible transfer).
     */
    GpuBuffer *newBuffer(int byteSize, const std::string &usage = "storage");

    /**
     * Record + submit a compute dispatch and wait for completion (sync).
     * groups*: workgroup counts (not thread counts).
     */
    void dispatch(ComputeShader *shader, int groupsX, int groupsY = 1, int groupsZ = 1);
};

}  // namespace eve::gpgpu
