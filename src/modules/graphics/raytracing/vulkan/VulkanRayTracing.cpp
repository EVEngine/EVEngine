#include "graphics/raytracing/vulkan/VulkanRayTracing.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "graphics/shaders/rt_reflection_rchit_spv.inc"
#include "graphics/shaders/rt_reflection_rgen_spv.inc"
#include "graphics/shaders/rt_reflection_rmiss_spv.inc"
#include "graphics/vulkan/Graphics.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace eve::graphics::raytracing {
namespace {

Result<void> unsupported(const char* op) {
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, std::string(op) + ": hardware ray tracing unavailable", "graphics.raytracing"));
}

Result<void> failed(const char* op, const char* detail) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Failed, std::string(op) + ": " + detail, "graphics.raytracing"));
}

Result<void> invalidArg(const char* op, const char* detail) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::string(op) + ": " + detail, "graphics.raytracing"));
}

uint32_t alignedSize(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

vk::TransformMatrixKHR toTransformMatrix(const glm::mat4& m) {
    // Vulkan expects a 3x4 row-major affine matrix.
    vk::TransformMatrixKHR out{};
    const float*           src = &m[0][0];
    out.matrix[0][0]           = src[0];
    out.matrix[0][1]           = src[4];
    out.matrix[0][2]           = src[8];
    out.matrix[0][3]           = src[12];
    out.matrix[1][0]           = src[1];
    out.matrix[1][1]           = src[5];
    out.matrix[1][2]           = src[9];
    out.matrix[1][3]           = src[13];
    out.matrix[2][0]           = src[2];
    out.matrix[2][1]           = src[6];
    out.matrix[2][2]           = src[10];
    out.matrix[2][3]           = src[14];
    return out;
}

}  // namespace

void DeviceAddressBuffer::release() {
    if (!device) return;
    if (buffer) (*device)->destroyBuffer(buffer, device->allocation_callbacks);
    if (memory) (*device)->freeMemory(memory, device->allocation_callbacks);
    buffer  = vk::Buffer{};
    memory  = vk::DeviceMemory{};
    size    = 0;
    address = 0;
    device  = nullptr;
}

void DeviceAddressBuffer::steal(DeviceAddressBuffer& o) noexcept {
    buffer    = o.buffer;
    memory    = o.memory;
    size      = o.size;
    address   = o.address;
    device    = o.device;
    o.buffer  = vk::Buffer{};
    o.memory  = vk::DeviceMemory{};
    o.size    = 0;
    o.address = 0;
    o.device  = nullptr;
}

Result<void> DeviceAddressBuffer::allocate(vkb::Device& dev, vk::DeviceSize bytes, vk::BufferUsageFlags usage) {
    if (bytes == 0) return invalidArg("DeviceAddressBuffer.allocate", "size is zero");
    release();
    device = &dev;

    vk::BufferCreateInfo ci{};
    ci.size        = bytes;
    ci.usage       = usage | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    ci.sharingMode = vk::SharingMode::eExclusive;
    buffer         = (*device)->createBuffer(ci, device->allocation_callbacks);
    size           = bytes;

    const auto memreq = (*device)->getBufferMemoryRequirements(buffer);

    vk::MemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.pNext          = &flagsInfo;
    allocInfo.allocationSize = memreq.size;
    allocInfo.memoryTypeIndex =
        device->physical_device.findMemoryTypeIndex(memreq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    memory = (*device)->allocateMemory(allocInfo, device->allocation_callbacks);
    (*device)->bindBufferMemory(buffer, memory, 0);

    vk::BufferDeviceAddressInfo addrInfo{};
    addrInfo.buffer = buffer;
    address         = (*device)->getBufferAddress(addrInfo);
    if (address == 0) {
        release();
        return failed("DeviceAddressBuffer.allocate", "getBufferAddress returned 0");
    }
    return Result<void>::success();
}

Result<void> DeviceAddressBuffer::upload(vk::CommandPool pool, vk::Queue queue, const void* data,
                                         vk::DeviceSize bytes) {
    if (!device || !buffer) return failed("DeviceAddressBuffer.upload", "buffer not allocated");
    if (!data || bytes == 0 || bytes > size) return invalidArg("DeviceAddressBuffer.upload", "invalid host data");

    using pfb = vk::MemoryPropertyFlagBits;
    vkb::GenericBuffer staging(*device, vk::BufferUsageFlagBits::eTransferSrc, bytes,
                               pfb::eHostVisible | pfb::eHostCoherent);
    staging.updateLocal(vkb::FrameSlot::gpuIdle(), data, bytes);

    auto cmd = (*device)->allocateCommandBuffers({pool, vk::CommandBufferLevel::ePrimary, 1}).front();
    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    vk::BufferCopy copy{};
    copy.size = bytes;
    cmd.copyBuffer(staging.buffer, buffer, copy);
    cmd.end();
    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    queue.submit(submit, vk::Fence{});
    queue.waitIdle();
    (*device)->freeCommandBuffers(pool, cmd);
    return Result<void>::success();
}

void AccelerationStructure::release() {
    if (device && handle) {
        (*device)->destroyAccelerationStructureKHR(handle, device->allocation_callbacks);
        handle = vk::AccelerationStructureKHR{};
    }
    storage.release();
    deviceAddress = 0;
    device        = nullptr;
}

void AccelerationStructure::steal(AccelerationStructure& o) noexcept {
    handle          = o.handle;
    storage         = std::move(o.storage);
    deviceAddress   = o.deviceAddress;
    device          = o.device;
    o.handle        = vk::AccelerationStructureKHR{};
    o.deviceAddress = 0;
    o.device        = nullptr;
}

VulkanRayTracing& vulkanRayTracing() {
    static VulkanRayTracing instance;
    return instance;
}

VulkanRayTracing::~VulkanRayTracing() { detachDevice(); }

bool VulkanRayTracing::ensureAttached() {
    if (device_ && caps_.rayTracingAvailable()) return true;
    auto* base = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!base) base = eve::graphics::Graphics::create();
    auto* vg = dynamic_cast<eve::graphics::vulkan::Graphics*>(base);
    if (!vg || !vg->supportsRayTracing()) {
        if (vg) caps_ = vg->rayTracingCaps();
        return false;
    }
    attachDevice(&vg->getDevice(), vg->rayTracingCaps(), vg->getUploadPool(),
                 vg->getDevice().getQueue(vkb::QueueType::graphics));
    return isAvailable();
}

bool VulkanRayTracing::isAvailable() const {
    if (device_ && caps_.rayTracingAvailable()) return true;
    // Const path: probe Graphics without mutating. Non-const callers use ensureAttached.
    auto* base = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    auto* vg   = dynamic_cast<eve::graphics::vulkan::Graphics*>(base);
    return vg && vg->supportsRayTracing();
}

RayTracingCaps VulkanRayTracing::caps() const {
    if (caps_.rayTracingAvailable()) return caps_;
    auto* base = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    auto* vg   = dynamic_cast<eve::graphics::vulkan::Graphics*>(base);
    return vg ? vg->rayTracingCaps() : RayTracingCaps{};
}

void VulkanRayTracing::attachDevice(vkb::Device* device, const RayTracingCaps& caps, vk::CommandPool uploadPool,
                                    vk::Queue graphicsQueue) {
    detachDevice();
    device_        = device;
    caps_          = caps;
    uploadPool_    = uploadPool;
    graphicsQueue_ = graphicsQueue;
}

void VulkanRayTracing::detachDevice() {
    clearScene();
    if (device_) {
        if (pipeline_) (*device_)->destroyPipeline(pipeline_, device_->allocation_callbacks);
        if (pipelineLayout_) (*device_)->destroyPipelineLayout(pipelineLayout_, device_->allocation_callbacks);
        if (setLayout_) (*device_)->destroyDescriptorSetLayout(setLayout_, device_->allocation_callbacks);
        if (descriptorPool_) (*device_)->destroyDescriptorPool(descriptorPool_, device_->allocation_callbacks);
        if (raygenModule_) (*device_)->destroyShaderModule(raygenModule_, device_->allocation_callbacks);
        if (missModule_) (*device_)->destroyShaderModule(missModule_, device_->allocation_callbacks);
        if (closestHitModule_) (*device_)->destroyShaderModule(closestHitModule_, device_->allocation_callbacks);
    }
    pipeline_         = vk::Pipeline{};
    pipelineLayout_   = vk::PipelineLayout{};
    setLayout_        = vk::DescriptorSetLayout{};
    descriptorPool_   = vk::DescriptorPool{};
    descriptorSet_    = vk::DescriptorSet{};
    raygenModule_     = vk::ShaderModule{};
    missModule_       = vk::ShaderModule{};
    closestHitModule_ = vk::ShaderModule{};
    sbtBuffer_.release();
    raygenRegion_   = vk::StridedDeviceAddressRegionKHR{};
    missRegion_     = vk::StridedDeviceAddressRegionKHR{};
    hitRegion_      = vk::StridedDeviceAddressRegionKHR{};
    callableRegion_ = vk::StridedDeviceAddressRegionKHR{};
    device_         = nullptr;
    uploadPool_     = vk::CommandPool{};
    graphicsQueue_  = vk::Queue{};
    caps_           = {};
}

void VulkanRayTracing::clearScene() {
    meshes_.clear();
    tlas_.release();
}

Result<uint32_t> VulkanRayTracing::addTriangleMesh(const float* positionsXYZ, int vertexCount, const uint32_t* indices,
                                                   int indexCount, const glm::mat4& transform) {
    if (!ensureAttached())
        return Result<uint32_t>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "addTriangleMesh: hardware ray tracing unavailable", "graphics.raytracing"));
    if (!positionsXYZ || vertexCount < 3 || !indices || indexCount < 3 || (indexCount % 3) != 0)
        return Result<uint32_t>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "addTriangleMesh: invalid geometry", "graphics.raytracing"));

    auto mesh         = std::make_unique<TriangleMeshRecord>();
    mesh->vertexCount = uint32_t(vertexCount);
    mesh->indexCount  = uint32_t(indexCount);
    mesh->transform   = transform;

    const vk::DeviceSize vBytes = vk::DeviceSize(vertexCount) * 3u * sizeof(float);
    const vk::DeviceSize iBytes = vk::DeviceSize(indexCount) * sizeof(uint32_t);
    const auto           usage  = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                  vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

    if (auto r = mesh->vertexBuffer.allocate(*device_, vBytes, usage); !r.ok())
        return Result<uint32_t>::failure(r.status());
    if (auto r = mesh->indexBuffer.allocate(*device_, iBytes, usage); !r.ok())
        return Result<uint32_t>::failure(r.status());
    if (auto r = mesh->vertexBuffer.upload(uploadPool_, graphicsQueue_, positionsXYZ, vBytes); !r.ok())
        return Result<uint32_t>::failure(r.status());
    if (auto r = mesh->indexBuffer.upload(uploadPool_, graphicsQueue_, indices, iBytes); !r.ok())
        return Result<uint32_t>::failure(r.status());
    if (auto r = buildBlas(*mesh); !r.ok()) return Result<uint32_t>::failure(r.status());

    const uint32_t id = uint32_t(meshes_.size());
    meshes_.push_back(std::move(mesh));
    return Result<uint32_t>::success(id);
}

Result<void> VulkanRayTracing::buildBlas(TriangleMeshRecord& mesh) {
    vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.vertexFormat             = vk::Format::eR32G32B32Sfloat;
    triangles.vertexData.deviceAddress = mesh.vertexBuffer.address;
    triangles.vertexStride             = sizeof(float) * 3;
    triangles.maxVertex                = mesh.vertexCount - 1;
    triangles.indexType                = vk::IndexType::eUint32;
    triangles.indexData.deviceAddress  = mesh.indexBuffer.address;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType       = vk::GeometryTypeKHR::eTriangles;
    geometry.flags              = vk::GeometryFlagBitsKHR::eOpaque;
    geometry.geometry.triangles = triangles;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type          = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags         = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    buildInfo.mode          = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geometry;

    const uint32_t primitiveCount = mesh.indexCount / 3;
    const auto sizes = (*device_)->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
                                                                         buildInfo, primitiveCount);

    auto storageResult = mesh.blas.storage.allocate(
        *device_, sizes.accelerationStructureSize,
        vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
    if (!storageResult.ok()) return storageResult;

    vk::AccelerationStructureCreateInfoKHR createInfo{};
    createInfo.buffer = mesh.blas.storage.buffer;
    createInfo.size   = sizes.accelerationStructureSize;
    createInfo.type   = vk::AccelerationStructureTypeKHR::eBottomLevel;
    mesh.blas.handle  = (*device_)->createAccelerationStructureKHR(createInfo, device_->allocation_callbacks);
    mesh.blas.device  = device_;

    vk::AccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.accelerationStructure = mesh.blas.handle;
    mesh.blas.deviceAddress        = (*device_)->getAccelerationStructureAddressKHR(addrInfo);

    DeviceAddressBuffer scratch;
    if (auto r =
            scratch.allocate(*device_, sizes.buildScratchSize,
                             vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        !r.ok())
        return r;

    buildInfo.dstAccelerationStructure  = mesh.blas.handle;
    buildInfo.scratchData.deviceAddress = scratch.address;

    vk::AccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount                                     = primitiveCount;
    const vk::AccelerationStructureBuildRangeInfoKHR* ranges = &range;

    auto cmd = (*device_)->allocateCommandBuffers({uploadPool_, vk::CommandBufferLevel::ePrimary, 1}).front();
    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.buildAccelerationStructuresKHR(buildInfo, ranges);
    cmd.end();
    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    graphicsQueue_.submit(submit, vk::Fence{});
    graphicsQueue_.waitIdle();
    (*device_)->freeCommandBuffers(uploadPool_, cmd);
    return Result<void>::success();
}

Result<void> VulkanRayTracing::buildTlas() {
    tlas_.release();
    if (meshes_.empty()) return Result<void>::success();

    std::vector<vk::AccelerationStructureInstanceKHR> instances;
    instances.reserve(meshes_.size());
    for (const auto& mesh : meshes_) {
        vk::AccelerationStructureInstanceKHR inst{};
        inst.transform                              = toTransformMatrix(mesh->transform);
        inst.instanceCustomIndex                    = 0;
        inst.mask                                   = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags                          = uint32_t(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
        inst.accelerationStructureReference = mesh->blas.deviceAddress;
        instances.push_back(inst);
    }

    DeviceAddressBuffer  instanceBuffer;
    const vk::DeviceSize bytes = vk::DeviceSize(instances.size()) * sizeof(instances[0]);
    if (auto r = instanceBuffer.allocate(*device_, bytes,
                                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                             vk::BufferUsageFlagBits::eShaderDeviceAddress |
                                             vk::BufferUsageFlagBits::eTransferDst);
        !r.ok())
        return r;
    if (auto r = instanceBuffer.upload(uploadPool_, graphicsQueue_, instances.data(), bytes); !r.ok()) return r;

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.arrayOfPointers    = VK_FALSE;
    instancesData.data.deviceAddress = instanceBuffer.address;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType       = vk::GeometryTypeKHR::eInstances;
    geometry.geometry.instances = instancesData;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type          = vk::AccelerationStructureTypeKHR::eTopLevel;
    buildInfo.flags         = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    buildInfo.mode          = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geometry;

    const uint32_t primitiveCount = uint32_t(instances.size());
    const auto sizes = (*device_)->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
                                                                         buildInfo, primitiveCount);

    if (auto r = tlas_.storage.allocate(
            *device_, sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        !r.ok())
        return r;

    vk::AccelerationStructureCreateInfoKHR createInfo{};
    createInfo.buffer = tlas_.storage.buffer;
    createInfo.size   = sizes.accelerationStructureSize;
    createInfo.type   = vk::AccelerationStructureTypeKHR::eTopLevel;
    tlas_.handle      = (*device_)->createAccelerationStructureKHR(createInfo, device_->allocation_callbacks);
    tlas_.device      = device_;

    vk::AccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.accelerationStructure = tlas_.handle;
    tlas_.deviceAddress            = (*device_)->getAccelerationStructureAddressKHR(addrInfo);

    DeviceAddressBuffer scratch;
    if (auto r =
            scratch.allocate(*device_, sizes.buildScratchSize,
                             vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        !r.ok())
        return r;

    buildInfo.dstAccelerationStructure  = tlas_.handle;
    buildInfo.scratchData.deviceAddress = scratch.address;

    vk::AccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount                                     = primitiveCount;
    const vk::AccelerationStructureBuildRangeInfoKHR* ranges = &range;

    auto cmd = (*device_)->allocateCommandBuffers({uploadPool_, vk::CommandBufferLevel::ePrimary, 1}).front();
    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.buildAccelerationStructuresKHR(buildInfo, ranges);
    cmd.end();
    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    graphicsQueue_.submit(submit, vk::Fence{});
    graphicsQueue_.waitIdle();
    (*device_)->freeCommandBuffers(uploadPool_, cmd);
    return Result<void>::success();
}

Result<void> VulkanRayTracing::rebuildScene() {
    if (!ensureAttached()) return unsupported("rebuildScene");
    return buildTlas();
}

Result<void> VulkanRayTracing::ensurePipeline() {
    if (pipeline_) return Result<void>::success();
    if (!isAvailable()) return unsupported("ensurePipeline");

    auto makeModule = [&](const uint32_t* words, size_t count) -> vk::ShaderModule {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = count * sizeof(uint32_t);
        ci.pCode    = words;
        return (*device_)->createShaderModule(ci, device_->allocation_callbacks);
    };
    raygenModule_     = makeModule(rt_reflection_rgen_spv, rt_reflection_rgen_spv_count);
    missModule_       = makeModule(rt_reflection_rmiss_spv, rt_reflection_rmiss_spv_count);
    closestHitModule_ = makeModule(rt_reflection_rchit_spv, rt_reflection_rchit_spv_count);

    vk::DescriptorSetLayoutBinding bindings[5]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = vk::DescriptorType::eAccelerationStructureKHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = vk::DescriptorType::eStorageImage;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR;
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR;
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR;
    bindings[4].binding         = 4;
    bindings[4].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR;

    vk::DescriptorSetLayoutCreateInfo layoutCi{};
    layoutCi.bindingCount = 5;
    layoutCi.pBindings    = bindings;
    setLayout_            = (*device_)->createDescriptorSetLayout(layoutCi, device_->allocation_callbacks);

    vk::PushConstantRange push{};
    push.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;
    push.offset     = 0;
    push.size       = sizeof(PushConstants);

    vk::PipelineLayoutCreateInfo plCi{};
    plCi.setLayoutCount         = 1;
    plCi.pSetLayouts            = &setLayout_;
    plCi.pushConstantRangeCount = 1;
    plCi.pPushConstantRanges    = &push;
    pipelineLayout_             = (*device_)->createPipelineLayout(plCi, device_->allocation_callbacks);

    std::array<vk::PipelineShaderStageCreateInfo, 3> stages{};
    stages[0].stage  = vk::ShaderStageFlagBits::eRaygenKHR;
    stages[0].module = raygenModule_;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eMissKHR;
    stages[1].module = missModule_;
    stages[1].pName  = "main";
    stages[2].stage  = vk::ShaderStageFlagBits::eClosestHitKHR;
    stages[2].module = closestHitModule_;
    stages[2].pName  = "main";

    std::array<vk::RayTracingShaderGroupCreateInfoKHR, 3> groups{};
    groups[0].type               = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    groups[0].generalShader      = 0;
    groups[0].closestHitShader   = VK_SHADER_UNUSED_KHR;
    groups[0].anyHitShader       = VK_SHADER_UNUSED_KHR;
    groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;
    groups[1].type               = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    groups[1].generalShader      = 1;
    groups[1].closestHitShader   = VK_SHADER_UNUSED_KHR;
    groups[1].anyHitShader       = VK_SHADER_UNUSED_KHR;
    groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;
    groups[2].type               = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    groups[2].generalShader      = VK_SHADER_UNUSED_KHR;
    groups[2].closestHitShader   = 2;
    groups[2].anyHitShader       = VK_SHADER_UNUSED_KHR;
    groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    vk::RayTracingPipelineCreateInfoKHR pipeCi{};
    pipeCi.stageCount = uint32_t(stages.size());
    pipeCi.pStages    = stages.data();
    pipeCi.groupCount = uint32_t(groups.size());
    pipeCi.pGroups    = groups.data();
    pipeCi.maxPipelineRayRecursionDepth =
        std::max(1u, std::min(1u, caps_.maxRecursionDepth ? caps_.maxRecursionDepth : 1u));
    pipeCi.layout = pipelineLayout_;

    auto created = (*device_)->createRayTracingPipelinesKHR(vk::DeferredOperationKHR{}, vk::PipelineCache{}, pipeCi,
                                                            device_->allocation_callbacks);
    if (created.result != vk::Result::eSuccess || created.value.empty())
        return failed("ensurePipeline", "createRayTracingPipelinesKHR failed");
    pipeline_ = created.value.front();

    return createShaderBindingTable();
}

Result<void> VulkanRayTracing::createShaderBindingTable() {
    const uint32_t handleSize      = caps_.shaderGroupHandleSize;
    const uint32_t handleAlignment = caps_.shaderGroupHandleAlignment ? caps_.shaderGroupHandleAlignment : handleSize;
    const uint32_t baseAlignment   = caps_.shaderGroupBaseAlignment ? caps_.shaderGroupBaseAlignment : handleAlignment;
    const uint32_t handleSizeAligned = alignedSize(handleSize, handleAlignment);
    const uint32_t groupCount        = 3;
    const uint32_t sbtSize           = groupCount * handleSizeAligned;

    std::vector<uint8_t> handles(sbtSize);
    auto result = (*device_)->getRayTracingShaderGroupHandlesKHR(pipeline_, 0, groupCount, sbtSize, handles.data());
    if (result != vk::Result::eSuccess)
        return failed("createShaderBindingTable", "getRayTracingShaderGroupHandlesKHR failed");

    // Pack groups with base alignment between regions: raygen | miss | hit
    const uint32_t raygenSize = alignedSize(handleSizeAligned, baseAlignment);
    const uint32_t missSize   = alignedSize(handleSizeAligned, baseAlignment);
    const uint32_t hitSize    = alignedSize(handleSizeAligned, baseAlignment);
    const uint32_t total      = raygenSize + missSize + hitSize;

    std::vector<uint8_t> sbt(total, 0);
    std::memcpy(sbt.data(), handles.data(), handleSize);
    std::memcpy(sbt.data() + raygenSize, handles.data() + handleSizeAligned, handleSize);
    std::memcpy(sbt.data() + raygenSize + missSize, handles.data() + 2 * handleSizeAligned, handleSize);

    if (auto r = sbtBuffer_.allocate(*device_, total,
                                     vk::BufferUsageFlagBits::eShaderBindingTableKHR |
                                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                                         vk::BufferUsageFlagBits::eTransferDst);
        !r.ok())
        return r;
    if (auto r = sbtBuffer_.upload(uploadPool_, graphicsQueue_, sbt.data(), total); !r.ok()) return r;

    raygenRegion_.deviceAddress = sbtBuffer_.address;
    raygenRegion_.stride        = raygenSize;
    raygenRegion_.size          = raygenSize;

    missRegion_.deviceAddress = sbtBuffer_.address + raygenSize;
    missRegion_.stride        = handleSizeAligned;
    missRegion_.size          = missSize;

    hitRegion_.deviceAddress = sbtBuffer_.address + raygenSize + missSize;
    hitRegion_.stride        = handleSizeAligned;
    hitRegion_.size          = hitSize;

    callableRegion_ = vk::StridedDeviceAddressRegionKHR{};
    return Result<void>::success();
}

Result<void> VulkanRayTracing::ensureDescriptorSets(vk::ImageView outputView, vk::ImageView sceneView,
                                                    vk::ImageView depthView, vk::ImageView normalView) {
    if (!descriptorPool_) {
        std::array<vk::DescriptorPoolSize, 3> sizes{};
        sizes[0] = {vk::DescriptorType::eAccelerationStructureKHR, 4};
        sizes[1] = {vk::DescriptorType::eStorageImage, 4};
        sizes[2] = {vk::DescriptorType::eCombinedImageSampler, 12};
        vk::DescriptorPoolCreateInfo poolCi{};
        poolCi.maxSets       = 4;
        poolCi.poolSizeCount = uint32_t(sizes.size());
        poolCi.pPoolSizes    = sizes.data();
        descriptorPool_      = (*device_)->createDescriptorPool(poolCi, device_->allocation_callbacks);
    }
    if (!descriptorSet_) {
        vk::DescriptorSetAllocateInfo alloc{};
        alloc.descriptorPool     = descriptorPool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &setLayout_;
        descriptorSet_           = (*device_)->allocateDescriptorSets(alloc).front();
    }

    vk::WriteDescriptorSetAccelerationStructureKHR asWrite{};
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures    = &tlas_.handle;

    vk::WriteDescriptorSet writes[5]{};
    writes[0].pNext           = &asWrite;
    writes[0].dstSet          = descriptorSet_;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = vk::DescriptorType::eAccelerationStructureKHR;

    // Storage image for output — sampler unused
    vk::DescriptorImageInfo outInfo{};
    outInfo.imageView         = outputView;
    outInfo.imageLayout       = vk::ImageLayout::eGeneral;
    writes[1].dstSet          = descriptorSet_;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = vk::DescriptorType::eStorageImage;
    writes[1].pImageInfo      = &outInfo;

    // Use a default nearest sampler from the device via a temporary immutable-less sampler.
    // Graphics white texture's sampler is not accessible; create a local sampler.
    static thread_local vk::Sampler s_sampler{};
    if (!s_sampler) {
        vk::SamplerCreateInfo sci{};
        sci.magFilter    = vk::Filter::eNearest;
        sci.minFilter    = vk::Filter::eNearest;
        sci.mipmapMode   = vk::SamplerMipmapMode::eNearest;
        sci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        sci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        s_sampler        = (*device_)->createSampler(sci, device_->allocation_callbacks);
    }

    vk::DescriptorImageInfo sceneInfo{};
    sceneInfo.sampler         = s_sampler;
    sceneInfo.imageView       = sceneView;
    sceneInfo.imageLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    writes[2].dstSet          = descriptorSet_;
    writes[2].dstBinding      = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[2].pImageInfo      = &sceneInfo;

    vk::DescriptorImageInfo depthInfo{};
    depthInfo.sampler         = s_sampler;
    depthInfo.imageView       = depthView;
    depthInfo.imageLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    writes[3].dstSet          = descriptorSet_;
    writes[3].dstBinding      = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[3].pImageInfo      = &depthInfo;

    vk::DescriptorImageInfo normalInfo{};
    normalInfo.sampler        = s_sampler;
    normalInfo.imageView      = normalView;
    normalInfo.imageLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;
    writes[4].dstSet          = descriptorSet_;
    writes[4].dstBinding      = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[4].pImageInfo      = &normalInfo;

    (*device_)->updateDescriptorSets(writes, {});
    return Result<void>::success();
}

Result<void> VulkanRayTracing::applyReflections(Graphics* gfx, Texture* sceneColor, Texture* hwDepth,
                                                Texture* worldNormal, Canvas* dest, const glm::mat4& invViewProj,
                                                const glm::vec3& eyeWorld) {
    if (!ensureAttached()) return unsupported("applyReflections");
    if (!gfx || !sceneColor || !hwDepth || !worldNormal || !dest)
        return invalidArg("applyReflections", "null argument");
    if (meshes_.empty() || !tlas_.handle) {
        // Empty scene: clear destination to transparent (SSR can fill).
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }

    auto* vkGfx = dynamic_cast<eve::graphics::vulkan::Graphics*>(gfx);
    if (!vkGfx) return failed("applyReflections", "requires Vulkan Graphics backend");

    if (auto r = ensurePipeline(); !r.ok()) return r;

    // Resolve texture image views through the vulkan Texture gpuHandle.
    auto textureView = [](Texture* tex) -> vk::ImageView {
        if (!tex || !tex->gpuHandle) return {};
        auto* img = static_cast<eve::graphics::vulkan::GpuTexture*>(tex->gpuHandle);
        return img->imageView();
    };

    // Destination must be a storage-capable image. Offscreen canvases in this
    // engine are typically sampled color attachments; for the first cut we
    // require the caller to pass a canvas whose color target can be transitioned
    // to GENERAL. When the view cannot be resolved, fail loudly.
    Texture* destTex = dest->getTexture();
    if (!destTex) return failed("applyReflections", "destination canvas has no texture");
    const vk::ImageView outView    = textureView(destTex);
    const vk::ImageView sceneView  = textureView(sceneColor);
    const vk::ImageView depthView  = textureView(hwDepth);
    const vk::ImageView normalView = textureView(worldNormal);
    if (!outView || !sceneView || !depthView || !normalView)
        return failed("applyReflections", "missing texture image view");

    if (auto r = ensureDescriptorSets(outView, sceneView, depthView, normalView); !r.ok()) return r;

    PushConstants push{};
    push.invViewProj = invViewProj;
    push.eye         = glm::vec4(eyeWorld, 0.f);

    auto cmd = (*device_)->allocateCommandBuffers({uploadPool_, vk::CommandBufferLevel::ePrimary, 1}).front();
    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, pipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pipelineLayout_, 0, descriptorSet_, {});
    cmd.pushConstants(pipelineLayout_, vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &push);
    const uint32_t width  = uint32_t(dest->getWidth());
    const uint32_t height = uint32_t(dest->getHeight());
    cmd.traceRaysKHR(raygenRegion_, missRegion_, hitRegion_, callableRegion_, width, height, 1);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    graphicsQueue_.submit(submit, vk::Fence{});
    graphicsQueue_.waitIdle();
    (*device_)->freeCommandBuffers(uploadPool_, cmd);
    return Result<void>::success();
}

namespace {

struct RegisterCapability {
    RegisterCapability() { eve::cap::provide<IRayTracing>(&vulkanRayTracing()); }
} g_register;

}  // namespace

}  // namespace eve::graphics::raytracing
