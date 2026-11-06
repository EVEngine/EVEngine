#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": Vulkan error " + std::to_string(result));
}
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize bytes = 0;
};
struct Counts {
    std::uint32_t sources, partials;
    float dt;
    std::uint32_t targets;
    float maxError;
    float sourceShift;
};
static_assert(sizeof(Counts) == 24);
using Clock = std::chrono::steady_clock;

std::vector<std::uint32_t> shader(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("missing SPIR-V: " + file.string());
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0) throw std::runtime_error("invalid SPIR-V size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(words.data()), size);
    if (!input || words[0] != 0x07230203) throw std::runtime_error("invalid SPIR-V contents");
    return words;
}

double quantile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(std::ceil(values.size() * fraction)) - 1)];
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("usage: gpu_field_probe <shader-dir> <1..1000000 rows> <1..1000 frames> [near|mixed]");
        const std::string mode = argc == 5 ? argv[4] : "far";
        if (mode != "far" && mode != "near" && mode != "mixed") throw std::runtime_error("unknown probe mode");
        const std::filesystem::path shaderDir = argv[1];
        const auto count = std::stoull(argv[2]);
        const auto frames = std::stoull(argv[3]);
        if (count == 0 || count > 1000000 || frames == 0 || frames > 1000)
            throw std::runtime_error("row or frame count outside probe bounds");
        const auto groups = static_cast<std::uint32_t>((count + 255) / 256);
        const Counts push{static_cast<std::uint32_t>(count), groups, 1.f / 60.f,
                          static_cast<std::uint32_t>(count), 1e-3f, 0.f};
        const auto sourceShift = [](std::size_t frame) { return frame & 1u ? -.001f : .001f; };

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "EVEngine GPU field probe";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        VkInstance instance = VK_NULL_HANDLE;
        require(vkCreateInstance(&instanceInfo, nullptr, &instance), "create instance");
        std::uint32_t deviceCount = 0;
        require(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "enumerate devices");
        if (!deviceCount) throw std::runtime_error("no Vulkan device");
        std::vector<VkPhysicalDevice> devices(deviceCount);
        require(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "list devices");
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        std::uint32_t family = 0;
        VkPhysicalDeviceProperties properties{};
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties candidateProperties{};
            vkGetPhysicalDeviceProperties(candidate, &candidateProperties);
            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (std::uint32_t index = 0; index < familyCount; ++index) {
                if (!(families[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
                if (!physical || candidateProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    physical = candidate;
                    family = index;
                    properties = candidateProperties;
                }
                break;
            }
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
        }
        if (!physical) throw std::runtime_error("no Vulkan compute queue");
        if (groups > properties.limits.maxComputeWorkGroupCount[0] ||
            count * sizeof(std::array<float, 4>) > properties.limits.maxStorageBufferRange)
            throw std::runtime_error("device workgroup or storage range exceeded");
        const float priority = 1.f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        VkDevice device = VK_NULL_HANDLE;
        require(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, family, 0, &queue);

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
        const auto makeBuffer = [&](VkDeviceSize bytes) {
            Buffer output;
            output.bytes = bytes;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = bytes;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            require(vkCreateBuffer(device, &info, nullptr, &output.buffer), "create buffer");
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, output.buffer, &requirements);
            std::uint32_t memoryType = memoryProperties.memoryTypeCount;
            for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
                const auto flags = memoryProperties.memoryTypes[i].propertyFlags;
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                    memoryType = i;
                    if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
                }
            }
            if (memoryType == memoryProperties.memoryTypeCount) throw std::runtime_error("no coherent host-visible memory");
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memoryType;
            require(vkAllocateMemory(device, &allocation, nullptr, &output.memory), "allocate memory");
            require(vkBindBufferMemory(device, output.buffer, output.memory, 0), "bind memory");
            require(vkMapMemory(device, output.memory, 0, bytes, 0, &output.mapped), "map memory");
            return output;
        };
        std::array<Buffer, 9> buffers{
            makeBuffer(count * sizeof(std::array<float, 4>)),
            makeBuffer(groups * 8 * sizeof(std::array<float, 4>)),
            makeBuffer(8 * sizeof(std::array<float, 4>)),
            makeBuffer(count * sizeof(std::array<float, 4>)),
            makeBuffer(count * sizeof(std::array<float, 4>)), makeBuffer(sizeof(std::uint32_t)),
            makeBuffer(count * sizeof(std::array<float, 4>)),
            makeBuffer(count * sizeof(std::array<float, 4>)),
            makeBuffer(count * sizeof(std::array<float, 4>))};
        std::vector<std::array<float, 4>> sources(count);
        auto* positions = static_cast<std::array<float, 4>*>(buffers[3].mapped);
        auto* velocities = static_cast<std::array<float, 4>*>(buffers[4].mapped);
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned cell = static_cast<unsigned>(i % 8);
            const float jitter = (static_cast<float>((i * 37) % 101) / 100.f - .5f) * .02f;
            sources[i] = {(cell & 1 ? .5f : -.5f) + jitter,
                          (cell & 2 ? .5f : -.5f) - jitter * .7f,
                          (cell & 4 ? .5f : -.5f) + jitter * .5f,
                          1.f / static_cast<float>(count)};
            const bool targetNear = mode == "near" || (mode == "mixed" && (i & 1u));
            positions[i] = {targetNear ? .5f : 10.f + static_cast<float>(i % 101) * .001f,
                            targetNear ? .5f : static_cast<float>((i / 101) % 101) * .001f,
                            targetNear ? .5f : .25f, 0.f};
            velocities[i] = {};
        }
        std::memcpy(buffers[0].mapped, sources.data(), count * sizeof(sources[0]));

        std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
        for (std::uint32_t i = 0; i < bindings.size(); ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        setInfo.pBindings = bindings.data();
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        require(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout), "descriptor layout");
        VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Counts)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &setLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        require(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "pipeline layout");
        std::array<VkPipeline, 3> pipelines{};
        for (std::size_t i = 0; i < pipelines.size(); ++i) {
            const auto words = shader(shaderDir / (std::array{"reduce.spv", "final.spv", "evaluate.spv"}[i]));
            VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = words.size() * 4;
            moduleInfo.pCode = words.data();
            VkShaderModule module = VK_NULL_HANDLE;
            require(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "shader module");
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = module;
            info.stage.pName = "main";
            info.layout = pipelineLayout;
            require(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines[i]), "compute pipeline");
            vkDestroyShaderModule(device, module, nullptr);
        }
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 18};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "descriptor pool");
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = pool;
        const std::array<VkDescriptorSetLayout, 2> setLayouts{setLayout, setLayout};
        alloc.descriptorSetCount = static_cast<std::uint32_t>(setLayouts.size());
        alloc.pSetLayouts = setLayouts.data();
        std::array<VkDescriptorSet, 2> sets{};
        require(vkAllocateDescriptorSets(device, &alloc, sets.data()), "descriptor sets");
        std::array<std::array<VkDescriptorBufferInfo, 9>, 2> bufferInfo{};
        std::array<std::array<VkWriteDescriptorSet, 9>, 2> writes{};
        for (std::size_t phase = 0; phase < sets.size(); ++phase) {
            const std::array<std::size_t, 9> slots = phase == 0
                ? std::array<std::size_t, 9>{0, 1, 2, 3, 4, 5, 6, 7, 8}
                : std::array<std::size_t, 9>{8, 1, 2, 6, 7, 5, 3, 4, 0};
            for (std::size_t i = 0; i < slots.size(); ++i) {
                const auto& buffer = buffers[slots[i]];
                bufferInfo[phase][i] = {buffer.buffer, 0, buffer.bytes};
                writes[phase][i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[phase][i].dstSet = sets[phase];
                writes[phase][i].dstBinding = static_cast<std::uint32_t>(i);
                writes[phase][i].descriptorCount = 1;
                writes[phase][i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[phase][i].pBufferInfo = &bufferInfo[phase][i];
            }
            vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes[phase].size()),
                                   writes[phase].data(), 0, nullptr);
        }
        VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = family;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        require(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool), "command pool");
        VkCommandBufferAllocateInfo commandAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAlloc.commandPool = commandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        require(vkAllocateCommandBuffers(device, &commandAlloc, &command), "command buffer");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        require(vkCreateFence(device, &fenceInfo, nullptr, &fence), "fence");

        std::vector<double> timings;
        timings.reserve(frames);
        for (std::size_t frame = 0; frame < frames + 4; ++frame) {
            auto framePush = push;
            framePush.sourceShift = sourceShift(frame);
            const auto phase = frame & 1u;
            const auto inputSource = phase == 0 ? 0u : 8u;
            const auto outputSource = phase == 0 ? 8u : 0u;
            const auto outputPosition = phase == 0 ? 6u : 3u;
            const auto outputVelocity = phase == 0 ? 7u : 4u;
            const auto* inputBefore = static_cast<const std::array<float, 4>*>(
                buffers[phase == 0 ? 3 : 6].mapped);
            const auto priorFirst = inputBefore[0];
            const auto priorSecond = inputBefore[std::min<std::size_t>(1, count - 1)];
            const auto* sourceBefore = static_cast<const std::array<float, 4>*>(buffers[inputSource].mapped);
            const auto priorSource = sourceBefore[0];
            const auto start = Clock::now();
            require(vkResetCommandBuffer(command, 0), "reset command");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            require(vkBeginCommandBuffer(command, &begin), "begin command");
            vkCmdFillBuffer(command, buffers[5].buffer, 0, sizeof(std::uint32_t), 0);
            VkBufferMemoryBarrier statusReady{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            statusReady.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            statusReady.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            statusReady.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            statusReady.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            statusReady.buffer = buffers[5].buffer;
            statusReady.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &statusReady, 0, nullptr);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                                    &sets[phase], 0, nullptr);
            vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(framePush), &framePush);
            const auto dispatch = [&](std::size_t pipeline, std::uint32_t workgroups) {
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pipeline]);
                vkCmdDispatch(command, workgroups, 1, 1);
            };
            const auto barrier = [&](VkBuffer buffer) {
                VkBufferMemoryBarrier memory{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                memory.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                memory.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                memory.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                memory.buffer = buffer;
                memory.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &memory, 0, nullptr);
            };
            dispatch(0, groups);
            barrier(buffers[1].buffer);
            dispatch(1, 8);
            barrier(buffers[2].buffer);
            dispatch(2, groups);
            std::array<VkBufferMemoryBarrier, 4> published{};
            const std::array<std::size_t, 4> publishedSlots{5, outputPosition, outputVelocity, outputSource};
            for (std::size_t i = 0; i < published.size(); ++i) {
                published[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                published[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                published[i].dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
                published[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                published[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                published[i].buffer = buffers[publishedSlots[i]].buffer;
                published[i].size = VK_WHOLE_SIZE;
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, static_cast<std::uint32_t>(published.size()), published.data(), 0,
                                 nullptr);
            require(vkEndCommandBuffer(command), "end command");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            require(vkQueueSubmit(queue, 1, &submit, fence), "submit");
            require(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL), "wait frame");
            require(vkResetFences(device, 1, &fence), "reset fence");
            if (*static_cast<const std::uint32_t*>(buffers[5].mapped) != 0) {
                const auto* input = static_cast<const std::array<float, 4>*>(
                    buffers[phase == 0 ? 3 : 6].mapped);
                const bool unchanged = input[0] == priorFirst &&
                                       input[std::min<std::size_t>(1, count - 1)] == priorSecond;
                const auto* publishedSource = static_cast<const std::array<float, 4>*>(buffers[inputSource].mapped);
                const bool sourceUnchanged = publishedSource[0] == priorSource;
                const auto* candidateSource = static_cast<const std::array<float, 4>*>(buffers[outputSource].mapped);
                const bool sourceCandidateChanged = candidateSource[0] != priorSource;
                const auto* candidate = static_cast<const std::array<float, 4>*>(
                    buffers[outputPosition].mapped);
                const bool farCandidateChanged = mode == "mixed" && candidate[0] != priorFirst;
                std::cerr << "published_state_unchanged=" << (unchanged && sourceUnchanged)
                          << " far_candidate_written=" << farCandidateChanged
                          << " source_candidate_written=" << sourceCandidateChanged << '\n';
                if (!unchanged || !sourceUnchanged || !sourceCandidateChanged ||
                    (mode == "mixed" && !farCandidateChanged))
                    throw std::runtime_error("GPU rejected frame damaged published state");
                throw std::runtime_error("GPU approximation rejected target; candidate discarded");
            }
            if (frame >= 4)
                timings.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        positions = static_cast<std::array<float, 4>*>(buffers[(frames + 4) % 2 == 0 ? 3 : 6].mapped);
        const auto* gpuSummaries = static_cast<const std::array<float, 4>*>(buffers[2].mapped);
        const auto displacedX = [&](std::size_t index, std::size_t frame) {
            const float displacement = (frame & 1u) == 0u ? .001f : 0.f;
            return sources[index][0] + ((index & 1u) == 0u ? displacement : -displacement);
        };
        std::array<std::array<std::array<double, 4>, 8>, 2> allCpuSummaries{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto& source = sources[i];
            const unsigned cell = unsigned(source[0] >= 0) | (unsigned(source[1] >= 0) << 1) |
                                  (unsigned(source[2] >= 0) << 2);
            for (unsigned phase = 0; phase < 2; ++phase) {
                auto& summary = allCpuSummaries[phase][cell];
                summary[0] += static_cast<double>(displacedX(i, phase)) * source[3];
                for (unsigned axis = 1; axis < 3; ++axis)
                    summary[axis] += static_cast<double>(source[axis]) * source[3];
                summary[3] += source[3];
            }
        }
        const auto& cpuSummaries = allCpuSummaries[(frames + 3) & 1u];
        const auto* finalSources = static_cast<const std::array<float, 4>*>(
            buffers[(frames + 4) % 2 == 0 ? 0 : 8].mapped);
        double maxSourceError = 0;
        for (std::size_t i = 0; i < count; ++i)
            maxSourceError = std::max(maxSourceError,
                                      std::abs(static_cast<double>(finalSources[i][0]) - displacedX(i, frames + 3)));
        double maxMomentError = 0, totalMass = 0;
        for (unsigned cell = 0; cell < 8; ++cell) {
            totalMass += gpuSummaries[cell][3];
            for (unsigned component = 0; component < 4; ++component)
                maxMomentError = std::max(maxMomentError,
                                          std::abs(static_cast<double>(gpuSummaries[cell][component]) -
                                                   cpuSummaries[cell][component]));
        }
        double maxClusterRadius = 0;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto& source = sources[i];
            const unsigned cell = unsigned(source[0] >= 0) | (unsigned(source[1] >= 0) << 1) |
                                  (unsigned(source[2] >= 0) << 2);
            double squared = 0;
            for (unsigned axis = 0; axis < 3; ++axis) {
                const double difference = static_cast<double>(axis == 0 ? displacedX(i, frames + 3) : source[axis]) -
                                          cpuSummaries[cell][axis] / cpuSummaries[cell][3];
                squared += difference * difference;
            }
            maxClusterRadius = std::max(maxClusterRadius, std::sqrt(squared));
        }
        const auto approximate = [&](const std::array<double, 3>& x, std::size_t frame) {
            std::array<double, 3> acceleration{};
            double bound = 0;
            for (const auto& summary : allCpuSummaries[frame & 1u]) {
                if (summary[3] <= 0) continue;
                std::array<double, 3> delta{};
                for (unsigned axis = 0; axis < 3; ++axis)
                    delta[axis] = summary[axis] / summary[3] - x[axis];
                const double distance = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
                const double factor = summary[3] / (distance * distance * distance);
                for (unsigned axis = 0; axis < 3; ++axis) acceleration[axis] += factor * delta[axis];
                bound += 2 * summary[3] * .04 / std::pow(distance - .04, 3);
            }
            return std::pair{acceleration, bound};
        };
        double maxPositionError = 0, maxDirectError = 0, maxBound = 0;
        const auto sampleCount = std::min<std::size_t>(16, count);
        for (std::size_t i = 0; i < sampleCount; ++i) {
            std::array<double, 3> x{10.0 + static_cast<double>(i % 101) * .001, 0.0, .25};
            std::array<double, 3> direct{};
            for (std::size_t sourceIndex = 0; sourceIndex < count; ++sourceIndex) {
                const auto& source = sources[sourceIndex];
                std::array<double, 3> delta{};
                for (unsigned axis = 0; axis < 3; ++axis)
                    delta[axis] = static_cast<double>(axis == 0 ? displacedX(sourceIndex, 0) : source[axis]) - x[axis];
                const double distance = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
                const double factor = source[3] / (distance * distance * distance);
                for (unsigned axis = 0; axis < 3; ++axis) direct[axis] += factor * delta[axis];
            }
            const auto [initial, bound] = approximate(x, 0);
            maxBound = std::max(maxBound, bound);
            maxDirectError = std::max(maxDirectError, std::hypot(direct[0] - initial[0],
                                                                 direct[1] - initial[1], direct[2] - initial[2]));
            std::array<double, 3> v{};
            for (std::size_t frame = 0; frame < frames + 4; ++frame) {
                const auto [acceleration, ignoredBound] = approximate(x, frame);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    v[axis] += acceleration[axis] / 60.0;
                    x[axis] += v[axis] / 60.0;
                }
            }
            for (unsigned axis = 0; axis < 3; ++axis)
                maxPositionError = std::max(maxPositionError, std::abs(x[axis] - positions[i][axis]));
        }
        std::cout << "device=" << properties.deviceName << " sources=" << count << " targets=" << count
                  << " frames=" << frames << " gpu_mass=" << totalMass << " max_moment_error=" << maxMomentError
                  << " max_cluster_radius=" << maxClusterRadius
                  << " max_direct_accel_error=" << maxDirectError << " max_analytic_bound=" << maxBound
                  << " max_source_position_error=" << maxSourceError
                  << " max_sample_position_error=" << maxPositionError << " wall_p50_ms=" << quantile(timings, .5)
                  << " wall_p95_ms=" << quantile(timings, .95) << " wall_max_ms=" << quantile(timings, 1) << '\n';
        if (!std::isfinite(totalMass) || std::abs(totalMass - 1) > 1e-4 || maxMomentError > 1e-4 ||
            !std::isfinite(maxClusterRadius) || maxClusterRadius > .04 ||
            !std::isfinite(maxDirectError) || maxDirectError > maxBound + 1e-6 || maxBound > push.maxError ||
            !std::isfinite(maxSourceError) || maxSourceError > 1e-6 ||
            !std::isfinite(maxPositionError) || maxPositionError > 1e-4)
            throw std::runtime_error("GPU result exceeds probe tolerance");

        require(vkDeviceWaitIdle(device), "device idle");
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDescriptorPool(device, pool, nullptr);
        for (auto pipeline : pipelines) vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        for (auto& buffer : buffers) {
            vkUnmapMemory(device, buffer.memory);
            vkDestroyBuffer(device, buffer.buffer, nullptr);
            vkFreeMemory(device, buffer.memory, nullptr);
        }
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
