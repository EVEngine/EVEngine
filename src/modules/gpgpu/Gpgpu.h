#pragma once

#include "common/Module.h"

#include <string>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

/**
 * @brief GPGPU module — compute shaders + storage buffers via the active Graphics backend.
 * Uses the graphics queue when no dedicated compute family exists (Apple/MoltenVK).
 *
 * Script: `gpgpu <- eve.Gpgpu(); shader <- gpgpu.newShader(glsl);`
 * ECS: `eve.ShaderSystem` / `eve.EcsShaderSystem` bridge entity float fields to SSBOs.
 */
class Gpgpu : public Module {
public:
    Module_REG(Gpgpu);
    Gpgpu() = default;
    ~Gpgpu() override = default;

    /** @brief True when the active Graphics backend can run compute (device initialized). */
    bool isAvailable() const;

    /** @brief Compile compute source for the active backend (Vulkan: GLSL via glslc). */
    ComputeShader *newShader(const std::string &source);

    /** @brief Load precompiled compute bytecode from Filesystem path (Vulkan: SPIR-V). */
    ComputeShader *newShaderFromBytecode(const std::string &path);

    /** @brief Vulkan SPIR-V compatibility wrapper → newShaderFromBytecode. */
    ComputeShader *newShaderFromSpvFile(const std::string &path);

    /**
     * @brief Allocate a GPU buffer.
     * usage: "storage" (SSBO, device-local) | "staging" (host-visible transfer).
     */
    GpuBuffer *newBuffer(int byteSize, const std::string &usage = "storage");

    /**
     * @brief Record + submit a compute dispatch and wait for completion (sync).
     * groups*: workgroup counts (not thread counts).
     */
    void dispatch(ComputeShader *shader, int groupsX, int groupsY = 1, int groupsZ = 1);
};

}  // namespace eve::gpgpu
