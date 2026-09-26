#pragma once

#include "common/Result.h"
#include "graphics/IRayTracing.h"
#include "graphics/RayTracingCaps.h"
#include "vkbuilder.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace eve::graphics {
class Canvas;
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::graphics::raytracing {

/**
 * @brief Device-local buffer with shader device address (for AS builds / SBT).
 * @ownership Owns the Vulkan buffer and memory; destroyed on release().
 */
struct DeviceAddressBuffer {
    vk::Buffer        buffer{};
    vk::DeviceMemory  memory{};
    vk::DeviceSize    size    = 0;
    vk::DeviceAddress address = 0;
    vkb::Device*      device  = nullptr;

    DeviceAddressBuffer() = default;
    ~DeviceAddressBuffer() { release(); }
    DeviceAddressBuffer(const DeviceAddressBuffer&)            = delete;
    DeviceAddressBuffer& operator=(const DeviceAddressBuffer&) = delete;
    DeviceAddressBuffer(DeviceAddressBuffer&& o) noexcept { steal(o); }
    DeviceAddressBuffer& operator=(DeviceAddressBuffer&& o) noexcept {
        if (this != &o) {
            release();
            steal(o);
        }
        return *this;
    }

    void release();
    void steal(DeviceAddressBuffer& o) noexcept;

    /** @brief Allocate a buffer usage that includes SHADER_DEVICE_ADDRESS. */
    [[nodiscard]] Result<void> allocate(vkb::Device& dev, vk::DeviceSize bytes, vk::BufferUsageFlags usage);

    /** @brief Upload host bytes via a staging buffer (stalls the queue). */
    [[nodiscard]] Result<void> upload(vk::CommandPool pool, vk::Queue queue, const void* data, vk::DeviceSize bytes);
};

/**
 * @brief Bottom- or top-level acceleration structure handle.
 */
struct AccelerationStructure {
    vk::AccelerationStructureKHR handle{};
    DeviceAddressBuffer          storage;
    vk::DeviceAddress            deviceAddress = 0;
    vkb::Device*                 device        = nullptr;

    AccelerationStructure() = default;
    ~AccelerationStructure() { release(); }
    AccelerationStructure(const AccelerationStructure&)            = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;
    AccelerationStructure(AccelerationStructure&& o) noexcept { steal(o); }
    AccelerationStructure& operator=(AccelerationStructure&& o) noexcept {
        if (this != &o) {
            release();
            steal(o);
        }
        return *this;
    }

    void release();
    void steal(AccelerationStructure& o) noexcept;
};

struct TriangleMeshRecord {
    DeviceAddressBuffer   vertexBuffer;
    DeviceAddressBuffer   indexBuffer;
    AccelerationStructure blas;
    uint32_t              vertexCount = 0;
    uint32_t              indexCount  = 0;
    glm::mat4             transform{1.f};
};

/**
 * @brief Vulkan implementation of IRayTracing (KHR ray-tracing pipeline).
 */
class VulkanRayTracing final : public IRayTracing {
public:
    VulkanRayTracing() = default;
    ~VulkanRayTracing() override;

    VulkanRayTracing(const VulkanRayTracing&)            = delete;
    VulkanRayTracing& operator=(const VulkanRayTracing&) = delete;

    bool           isAvailable() const override;
    RayTracingCaps caps() const override;

    [[nodiscard]] Result<void> applyReflections(Graphics* gfx, Texture* sceneColor, Texture* hwDepth,
                                                Texture* worldNormal, Canvas* dest, const glm::mat4& invViewProj,
                                                const glm::vec3& eyeWorld) override;

    [[nodiscard]] Result<void> rebuildScene() override;

    [[nodiscard]] Result<uint32_t> addTriangleMesh(const float* positionsXYZ, int vertexCount, const uint32_t* indices,
                                                   int indexCount, const glm::mat4& transform) override;

    void clearScene() override;

    /** @brief Bind to the live Vulkan Graphics device (call after init). */
    void attachDevice(vkb::Device* device, const RayTracingCaps& caps, vk::CommandPool uploadPool,
                      vk::Queue graphicsQueue);

    /** @brief Drop GPU resources when Graphics tears down. */
    void detachDevice();

    /** @brief Lazily bind to the process Graphics singleton when RT is available. */
    bool ensureAttached();

private:
    [[nodiscard]] Result<void> ensurePipeline();
    [[nodiscard]] Result<void> ensureDescriptorSets(vk::ImageView outputView, vk::ImageView sceneView,
                                                    vk::ImageView depthView, vk::ImageView normalView);
    [[nodiscard]] Result<void> buildBlas(TriangleMeshRecord& mesh);
    [[nodiscard]] Result<void> buildTlas();
    [[nodiscard]] Result<void> createShaderBindingTable();

    vkb::Device*    device_ = nullptr;
    vk::CommandPool uploadPool_{};
    vk::Queue       graphicsQueue_{};
    RayTracingCaps  caps_{};

    std::vector<std::unique_ptr<TriangleMeshRecord>> meshes_;
    AccelerationStructure                            tlas_;

    vk::DescriptorSetLayout setLayout_{};
    vk::PipelineLayout      pipelineLayout_{};
    vk::Pipeline            pipeline_{};
    vk::DescriptorPool      descriptorPool_{};
    vk::DescriptorSet       descriptorSet_{};

    DeviceAddressBuffer               sbtBuffer_;
    vk::StridedDeviceAddressRegionKHR raygenRegion_{};
    vk::StridedDeviceAddressRegionKHR missRegion_{};
    vk::StridedDeviceAddressRegionKHR hitRegion_{};
    vk::StridedDeviceAddressRegionKHR callableRegion_{};

    vk::ShaderModule raygenModule_{};
    vk::ShaderModule missModule_{};
    vk::ShaderModule closestHitModule_{};

    struct PushConstants {
        glm::mat4 invViewProj;
        glm::vec4 eye;  // xyz = eye, w unused
    };
};

/** @brief Process-wide Vulkan RT backend; also registered as IRayTracing. */
VulkanRayTracing& vulkanRayTracing();

}  // namespace eve::graphics::raytracing
