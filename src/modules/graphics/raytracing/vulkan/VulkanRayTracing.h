#pragma once

#include "common/Result.h"
#include "graphics/IRayTracing.h"
#include "graphics/RayTracingCaps.h"
#include "vkbuilder.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace eve::graphics {
class Canvas;
class Graphics;
class Mesh;
class Texture;
}  // namespace eve::graphics

namespace eve::graphics::vulkan {
class Graphics;
}

namespace eve::graphics::raytracing {

namespace path {
struct Constants;
}

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

    /** @brief Constructs a DeviceAddressBuffer. */
    DeviceAddressBuffer() = default;
    /** @brief Releases DeviceAddressBuffer resources. */
    ~DeviceAddressBuffer() { release(); }
    DeviceAddressBuffer(const DeviceAddressBuffer&)            = delete;
    DeviceAddressBuffer& operator=(const DeviceAddressBuffer&) = delete;
    /** @brief Constructs a DeviceAddressBuffer. */
    DeviceAddressBuffer(DeviceAddressBuffer&& o) noexcept { steal(o); }
    /** @brief Operator =. */
    DeviceAddressBuffer& operator=(DeviceAddressBuffer&& o) noexcept {
        if (this != &o) {
            /** @brief Release. */
            release();
            /** @brief Steal. */
            steal(o);
        }
        return *this;
    }

    /** @brief Release. */
    void release();
    /** @brief Steal. */
    void steal(DeviceAddressBuffer& o) noexcept;

    /** @brief Ensure buffer capacity with usage including SHADER_DEVICE_ADDRESS.
     * @ownership Reuses compatible storage; caller must retire prior GPU accesses before writing or reallocating.
     */
    [[nodiscard]] Result<void> allocate(vkb::Device& dev, vk::DeviceSize bytes, vk::BufferUsageFlags usage);

    /** @brief Upload host bytes via a staging buffer (stalls the queue). */
    [[nodiscard]] Result<void> upload(vk::CommandPool pool, vk::Queue queue, const void* data, vk::DeviceSize bytes);

    /** @brief Allocate coherent host-visible storage and initialize it without a transfer submission.
     * @ownership Copies borrowed bytes during the call; owns the resulting buffer and memory.
     * Caller must retire prior GPU accesses before replacing these bytes.
     * @cost Host copy per snapshot; GPU allocation only when capacity or memory requirements change.
     */
    [[nodiscard]] Result<void> initializeHostVisible(vkb::Device& dev, const void* data, vk::DeviceSize bytes,
                                                     vk::BufferUsageFlags usage);

private:
    vk::BufferUsageFlags       usage_{};
    vk::MemoryPropertyFlags    properties_{};
    [[nodiscard]] Result<void> allocateWithProperties(vkb::Device& dev, vk::DeviceSize bytes,
                                                      vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
};

/**
 * @brief Bottom- or top-level acceleration structure handle.
 */
struct AccelerationStructure {
    vk::AccelerationStructureKHR handle{};
    DeviceAddressBuffer          storage;
    vk::DeviceAddress            deviceAddress = 0;
    vkb::Device*                 device        = nullptr;

    /** @brief Constructs a AccelerationStructure. */
    AccelerationStructure() = default;
    /** @brief Releases AccelerationStructure resources. */
    ~AccelerationStructure() { release(); }
    AccelerationStructure(const AccelerationStructure&)            = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;
    /** @brief Constructs a AccelerationStructure. */
    AccelerationStructure(AccelerationStructure&& o) noexcept { steal(o); }
    /** @brief Operator =. */
    AccelerationStructure& operator=(AccelerationStructure&& o) noexcept {
        if (this != &o) {
            /** @brief Release. */
            release();
            /** @brief Steal. */
            steal(o);
        }
        return *this;
    }

    /** @brief Release. */
    void release();
    /** @brief Steal. */
    void steal(AccelerationStructure& o) noexcept;
};

/** @brief TriangleMeshRecord public API. */
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
    /** @brief Constructs a VulkanRayTracing. */
    VulkanRayTracing() = default;
    /** @brief Releases VulkanRayTracing resources. */
    ~VulkanRayTracing() override;

    VulkanRayTracing(const VulkanRayTracing&)            = delete;
    VulkanRayTracing& operator=(const VulkanRayTracing&) = delete;

    /** @brief True when available. */
    bool           isAvailable() const override;
    /** @brief Caps. */
    RayTracingCaps caps() const override;

    /** @brief Prepare and publish a path-tracing scene.
     * @ownership Inputs are borrowed for this call; GPU snapshot is backend-owned.
     * @thread Render thread, outside frame recording; no reentrancy.
     * @cost GPU uploads/builds proportional to geometry, amortized across frames.
     */
    [[nodiscard]] Result<PathSceneToken> preparePathScene(Graphics* gfx, const PathSceneDescription& scene) override;
    /** @brief Replace instances while retaining prepared geometry.
     * @ownership Input is borrowed for the call; the provider retains geometry resources.
     * @thread Render thread, outside frame recording; no reentrancy.
     * @cost Rebuilds the instance hierarchy and emitter data per changed instance set, reusing BLAS.
     */
    [[nodiscard]] Result<PathSceneToken> updatePathInstances(PathSceneToken                token,
                                                             std::span<const PathInstance> instances) override;
    /** @brief Render a prepared sample batch.
     * @ownership Destination is borrowed for the call; history is backend-owned.
     * @thread Render thread, outside frame recording; no reentrancy.
     * @cost GPU work proportional to pixels, samples and bounces; waits once per batch.
     */
    [[nodiscard]] Result<void> renderPaths(PathSceneToken token, Canvas* dest, const PathTraceView& view) override;
    /** @brief Prepare owned normal-frame output. @copydetails IRayTracing::preparePathFrame */
    [[nodiscard]] Result<PathFrameToken> preparePathFrame(PathSceneToken scene, uint32_t width,
                                                          uint32_t height) override;
    /** @brief Record normal-frame tracing. @copydetails IRayTracing::recordPathFrame */
    [[nodiscard]] Result<Texture*> recordPathFrame(PathSceneToken scene, PathFrameToken output,
                                                   const PathTraceView& view) override;

    /** @brief Applies reflections. */
    [[nodiscard]] Result<void> applyReflections(Graphics* gfx, Texture* sceneColor, Texture* hwDepth,
                                                Texture* worldNormal, Canvas* dest, const glm::mat4& invViewProj,
                                                const glm::mat4& viewProj, const glm::vec3& eyeWorld) override;

    /** @brief Rebuild scene. */
    [[nodiscard]] Result<void> rebuildScene() override;

    /** @brief Adds triangle mesh. */
    [[nodiscard]] Result<uint32_t> addTriangleMesh(const float* positionsXYZ, int vertexCount, const uint32_t* indices,
                                                   int indexCount, const glm::mat4& transform) override;

    /** @brief Adds mesh. */
    [[nodiscard]] Result<uint32_t> addMesh(Mesh* mesh, const glm::mat4& transform) override;

    /** @brief Clears scene. */
    void clearScene() override;

    /** @brief Detaches from graphics. */
    void detachFromGraphics() override { detachDevice(); }

    /**
     * @brief Bind to the live Vulkan Graphics device (call after init).
     * @ownership `device` is borrowed; Graphics remains the owner.
     * @lifetime `device`, `uploadPool`, and `graphicsQueue` must outlive this
     *           backend until `detachDevice()`.
     * @thread Render thread only.
     */
    void attachDevice(vkb::Device* device, const RayTracingCaps& caps, vk::CommandPool uploadPool,
                      vk::Queue graphicsQueue);

    /** @brief Drop GPU resources when Graphics tears down. */
    void detachDevice();

    /**
     * @brief Lazily bind to the process Graphics singleton when RT is available.
     * @return Success when attached and RT is usable; Unsupported otherwise.
     */
    [[nodiscard]] Result<void> ensureAttached();

private:
    struct PathEmitter {
        glm::vec3 a, b, c, emission;
        uint32_t  primitive = 0;
    };
    struct PathGeometryRecord {
        uint32_t                 firstTriangle = 0;
        uint32_t                 firstVertex   = 0;
        bool                     alphaCoverage = false;
        std::vector<PathEmitter> emitters;
    };
    struct TlasWorkspace {
        DeviceAddressBuffer instances;
        DeviceAddressBuffer scratch;
    };
    struct PathScene {
        std::vector<PathLight>                           lights;
        std::vector<PathGeometryRecord>                  geometry;
        std::vector<std::unique_ptr<TriangleMeshRecord>> meshes;
        AccelerationStructure                            tlas;
        TlasWorkspace                                    workspace;
        std::array<DeviceAddressBuffer, 7>               buffers;
        uint32_t                                         lightCount = 0;
        uint64_t                                         generation = 0;
    };
    [[nodiscard]] Result<PathSceneToken> updatePathInstancesPrepared(std::span<const PathInstance> instances,
                                                                     std::vector<uint32_t>         instanceWords);
    [[nodiscard]] Result<void> buildPathInstances(std::span<const PathInstance> instances, const PathScene& geometry,
                                                  PathScene& destination);
    std::unique_ptr<PathScene> pathScene_;
    // Exact value keys, not hashes or retained caller pointers. Published only
    // with successful scene/instance transactions and cleared on device detach.
    std::vector<uint32_t> pathSceneWords_, pathInstanceWords_, pathMeshSceneWords_;
    // Borrowed from the prepared scene's device owner, cleared before detach.
    eve::graphics::vulkan::Graphics* pathGraphics_ = nullptr;
    // Retired snapshot storage: never written while it is the published scene.
    std::unique_ptr<PathScene> pathSpareScene_;
    uint64_t                   pathGeneration_     = 0;
    uint32_t                   pathHistorySamples_ = 0;
    PathTraceView              pathPreviousView_;
    // Published only after pipeline creation and SBT upload both succeed.
    struct PathPipeline {
        vkb::Device*                                     device = nullptr;
        vk::DescriptorSetLayout                          setLayout{};
        vk::PipelineLayout                               layout{};
        vk::Pipeline                                     pipeline{};
        vk::Pipeline                                     denoisePipeline{};
        vk::ShaderModule                                 denoiseModule{};
        vk::Pipeline                                     temporalPipeline{};
        vk::ShaderModule                                 temporalModule{};
        std::array<vk::ShaderModule, 4>                  modules{};
        DeviceAddressBuffer                              sbt;
        std::array<vk::StridedDeviceAddressRegionKHR, 4> regions{};

        ~PathPipeline();
    };
    [[nodiscard]] Result<void>    ensurePathPipeline();
    std::unique_ptr<PathPipeline> pathPipeline_;

    struct PathImage : vkb::GenericImage {
        using vkb::GenericImage::create;
    };
    struct PathFrame {
        struct OutputTextures;
        std::unique_ptr<OutputTextures>         output;
        vkb::Device*                            device = nullptr;
        std::array<PathImage, 10>               images;
        vk::DescriptorPool                      pool{};
        std::array<vk::DescriptorSet, 2>        sets{};
        uint32_t                                nextTemporalSlot = 0;
        vk::AccelerationStructureKHR            scene{};
        std::array<vk::DescriptorBufferInfo, 7> buffers{};
        uint32_t                                width         = 0;
        uint32_t                                height        = 0;
        bool                                    initialized   = false;
        bool                                    sampledOutput = false;
        ~PathFrame();
    };
    std::unique_ptr<PathFrame> pathFrame_;
    uint64_t                   pathFrameGeneration_  = 0;
    uint64_t                   pathRecordedSequence_ = 0;
    void                       reconcilePathRecording();
    [[nodiscard]] Result<void> validatePathView(const PathTraceView& view) const;
    [[nodiscard]] Result<void> ensurePathFrame(uint32_t width, uint32_t height);
    [[nodiscard]] Result<void> recordPathDispatch(vk::CommandBuffer command, vk::AccelerationStructureKHR scene,
                                                  const std::array<vk::DescriptorBufferInfo, 7>& buffers,
                                                  const path::Constants& constants, const PathTraceView& view,
                                                  uint32_t width, uint32_t height);

    [[nodiscard]] Result<void> ensurePipeline();
    [[nodiscard]] Result<void> ensureSampler();
    [[nodiscard]] Result<void> ensureOutputImage(uint32_t width, uint32_t height);
    [[nodiscard]] Result<void> ensureDescriptorSets(vk::ImageView outputView, vk::ImageView sceneView,
                                                    vk::ImageView depthView, vk::ImageView normalView);
    [[nodiscard]] Result<void> buildBlas(TriangleMeshRecord& mesh);
    [[nodiscard]] Result<void> buildTlas();
    [[nodiscard]] Result<void> buildTlas(std::span<const vk::AccelerationStructureInstanceKHR> instances,
                                         AccelerationStructure& target, TlasWorkspace& workspace);
    [[nodiscard]] Result<void> buildTlas(std::span<const vk::AccelerationStructureInstanceKHR> instances,
                                         AccelerationStructure&                                target);
    [[nodiscard]] Result<void> createShaderBindingTable();

    /** @brief Device-local RGBA16F storage target for traceRays (HDR canvases lack STORAGE usage). */
    struct StorageColorImage : vkb::GenericImage {
        /** @brief Allocate. */
        void allocate(vkb::Device& device, uint32_t width, uint32_t height) {
            vk::ImageCreateInfo info{};
            info.imageType     = vk::ImageType::e2D;
            info.format        = vk::Format::eR16G16B16A16Sfloat;
            info.extent        = vk::Extent3D{width, height, 1};
            info.mipLevels     = 1;
            info.arrayLayers   = 1;
            info.samples       = vk::SampleCountFlagBits::e1;
            info.tiling        = vk::ImageTiling::eOptimal;
            info.usage         = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc |
                                 vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
            info.sharingMode   = vk::SharingMode::eExclusive;
            info.initialLayout = vk::ImageLayout::eUndefined;
            /** @brief Creates . */
            create(device, info, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor, false);
        }
    };

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
    vk::Sampler             sampler_{};

    StorageColorImage outputImage_{};
    uint32_t          outputWidth_  = 0;
    uint32_t          outputHeight_ = 0;

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
        glm::mat4 viewProj;
        glm::vec4 eye;  // xyz = eye, w unused
    };
};

/** @brief Process-wide Vulkan RT backend; also registered as IRayTracing. */
VulkanRayTracing& vulkanRayTracing();

}  // namespace eve::graphics::raytracing
