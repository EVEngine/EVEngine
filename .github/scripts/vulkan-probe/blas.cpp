// Standalone driver contract: build one triangle BLAS without engine state.
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define VK_CHECK(call)                                       \
    do {                                                     \
        const VkResult result = (call);                      \
        if (result != VK_SUCCESS) {                          \
            std::fprintf(stderr, "%s: %d\n", #call, result); \
            std::exit(1);                                    \
        }                                                    \
    } while (false)

int main() {
    const VkApplicationInfo application{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "ARM64 BLAS probe", 0, nullptr, 0, VK_API_VERSION_1_2};
    const char*          layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo    = &application;
    instanceInfo.enabledLayerCount   = 1;
    instanceInfo.ppEnabledLayerNames = layers;
    VkInstance instance{};
    VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (count == 0) return 1;
    std::vector<VkPhysicalDevice> physicalDevices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, physicalDevices.data()));
    const VkPhysicalDevice                             physical = physicalDevices.front();
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &acceleration};
    vkGetPhysicalDeviceProperties2(physical, &properties);
    std::printf("Device %s; scratch alignment %u\n", properties.properties.deviceName,
                acceleration.minAccelerationStructureScratchOffsetAlignment);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    if (family == count) return 1;
    const float                   priority = 1;
    const VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &priority};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    features.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features};
    vulkan12.bufferDeviceAddress    = VK_TRUE;
    const char*        extensions[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                       VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &vulkan12};
    deviceInfo.queueCreateInfoCount    = 1;
    deviceInfo.pQueueCreateInfos       = &queueInfo;
    deviceInfo.enabledExtensionCount   = 2;
    deviceInfo.ppEnabledExtensionNames = extensions;
    VkDevice device{};
    VK_CHECK(vkCreateDevice(physical, &deviceInfo, nullptr, &device));
    VkQueue queue{};
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    struct Buffer {
        VkBuffer        buffer;
        VkDeviceMemory  memory;
        VkDeviceAddress address;
    };
    std::vector<Buffer> buffers;
    auto                allocate = [&](VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
        Buffer                   buffer{};
        const VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      nullptr,
                                      0,
                                      size,
                                      usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                      VK_SHARING_MODE_EXCLUSIVE};
        VK_CHECK(vkCreateBuffer(device, &info, nullptr, &buffer.buffer));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
        uint32_t   type     = 0;
        const auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        while (type < memoryProperties.memoryTypeCount &&
               (!(requirements.memoryTypeBits & (1u << type)) ||
                (memoryProperties.memoryTypes[type].propertyFlags & required) != required))
            ++type;
        if (type == memoryProperties.memoryTypeCount) std::exit(1);
        const VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr,
                                              VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
        const VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags, requirements.size, type};
        VK_CHECK(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory));
        VK_CHECK(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0));
        if (data) {
            void* mapped = nullptr;
            VK_CHECK(vkMapMemory(device, buffer.memory, 0, size, 0, &mapped));
            std::memcpy(mapped, data, size);
            vkUnmapMemory(device, buffer.memory);
        }
        const VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr,
                                                    buffer.buffer};
        buffer.address = vkGetBufferDeviceAddress(device, &addressInfo);
        buffers.push_back(buffer);
        return buffer;
    };
    const float    vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const uint32_t indices[]  = {0, 1, 2};
    const auto     vertex =
        allocate(sizeof(vertices), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, vertices);
    const auto index =
        allocate(sizeof(indices), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, indices);
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType              = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags                     = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles        = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    auto& triangles                    = geometry.geometry.triangles;
    triangles.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex.address;
    triangles.vertexStride             = 3 * sizeof(float);
    triangles.maxVertex                = 2;
    triangles.indexType                = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress  = index.address;
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries   = &geometry;
    auto sizesFn        = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    auto createFn = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    auto buildFn = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
    if (!sizesFn || !createFn || !buildFn) return 1;
    const uint32_t                           primitives = 1;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    sizesFn(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitives, &sizes);
    const auto storage =
        allocate(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, nullptr);
    VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    create.buffer = storage.buffer;
    create.size   = sizes.accelerationStructureSize;
    create.type   = build.type;
    VK_CHECK(createFn(device, &create, nullptr, &build.dstAccelerationStructure));
    const VkDeviceSize alignment = acceleration.minAccelerationStructureScratchOffsetAlignment;
    if (alignment == 0) return 1;
    const auto scratch = allocate(sizes.buildScratchSize + alignment - 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    build.scratchData.deviceAddress = (scratch.address + alignment - 1) & ~(alignment - 1);
    std::printf("vertices=%llx indices=%llx scratch=%llx size=%llu\n", static_cast<unsigned long long>(vertex.address),
                static_cast<unsigned long long>(index.address),
                static_cast<unsigned long long>(build.scratchData.deviceAddress),
                static_cast<unsigned long long>(sizes.buildScratchSize));
    std::fflush(stdout);
    const VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0, family};
    VkCommandPool                 pool{};
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
    const VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, pool,
                                                  VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer                   command{};
    VK_CHECK(vkAllocateCommandBuffers(device, &commandInfo, &command));
    const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(command, &begin));
    const VkAccelerationStructureBuildRangeInfoKHR range{1, 0, 0, 0};
    const auto*                                    ranges = &range;
    buildFn(command, 1, &build, &ranges);
    VK_CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &command;
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    std::puts("Standalone BLAS build passed");
    auto destroyFn = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    destroyFn(device, build.dstAccelerationStructure, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    for (const auto& buffer : buffers) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        vkFreeMemory(device, buffer.memory, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
