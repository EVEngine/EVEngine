#include "VulkanProbeCompute.h"
#include <shaderc/shaderc.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void check(VkResult r) {
    if (r != VK_SUCCESS) throw std::runtime_error("Vulkan failure: " + std::to_string(r));
}
class VulkanCompute final : public eve::tensor::OnnxCompute {
    size_t                           dispatches = 0;
    VkInstance                       instance   = VK_NULL_HANDLE;
    VkPhysicalDevice                 physical   = VK_NULL_HANDLE;
    VkDevice                         device     = VK_NULL_HANDLE;
    VkQueue                          queue      = VK_NULL_HANDLE;
    VkCommandPool                    commands   = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    struct Resources {
        VkDevice                    device;
        VkCommandPool               pool;
        std::vector<VkBuffer>       buffers;
        std::vector<VkDeviceMemory> memories;
        VkShaderModule              shader      = VK_NULL_HANDLE;
        VkDescriptorSetLayout       setLayout   = VK_NULL_HANDLE;
        VkPipelineLayout            layout      = VK_NULL_HANDLE;
        VkPipeline                  pipeline    = VK_NULL_HANDLE;
        VkDescriptorPool            descriptors = VK_NULL_HANDLE;
        VkCommandBuffer             command     = VK_NULL_HANDLE;
        VkFence                     fence       = VK_NULL_HANDLE;
        ~Resources() {
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (command) vkFreeCommandBuffers(device, pool, 1, &command);
            if (descriptors) vkDestroyDescriptorPool(device, descriptors, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
            if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            if (shader) vkDestroyShaderModule(device, shader, nullptr);
            for (auto b : buffers) vkDestroyBuffer(device, b, nullptr);
            for (auto m : memories) vkFreeMemory(device, m, nullptr);
        }
    };

public:
    ~VulkanCompute() override {
        if (commands) vkDestroyCommandPool(device, commands, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    void initialize() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "EVEngine ONNX GPU parity";
        app.apiVersion       = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        check(vkCreateInstance(&ci, nullptr, &instance));
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        uint32_t family = UINT32_MAX;
        for (auto d : devices) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(d, &p);
            if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                p.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                continue;
            uint32_t n = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
            std::vector<VkQueueFamilyProperties> qs(n);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qs.data());
            for (uint32_t i = 0; i < n; ++i)
                if (qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    physical = d;
                    family   = i;
                    break;
                }
            if (physical) {
                std::cout << "ONNX_GPU_DEVICE " << p.deviceName << '\n';
                break;
            }
        }
        if (!physical) throw std::runtime_error("No hardware Vulkan compute device");
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        float                   priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount       = 1;
        q.pQueuePriorities = &priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dc.queueCreateInfoCount = 1;
        dc.pQueueCreateInfos    = &q;
        check(vkCreateDevice(physical, &dc, nullptr, &device));
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pc, nullptr, &commands));
    }
    eve::Result<std::vector<uint8_t>> dispatch(const eve::tensor::OnnxKernel& k) override {
        try {
            Resources r{device, commands};
            if (k.inputs.size() >= 8 || !k.outputBytes || !k.workItems || uint64_t(k.workItems) + 63 > 65535u * 64u)
                throw std::runtime_error("Invalid probe kernel extent");
            const uint32_t                            n = static_cast<uint32_t>(k.inputs.size() + 1);
            std::vector<VkDescriptorSetLayoutBinding> bindings(n);
            std::vector<VkDescriptorBufferInfo>       infos(n);
            for (uint32_t i = 0; i < n; ++i) {
                const size_t       bytes = i + 1 == n ? k.outputBytes : k.inputs[i].size();
                VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bc.size  = std::max(size_t(4), (bytes + 3) & ~size_t(3));
                bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                VkBuffer b;
                check(vkCreateBuffer(device, &bc, nullptr, &b));
                r.buffers.push_back(b);
                VkMemoryRequirements req;
                vkGetBufferMemoryRequirements(device, b, &req);
                uint32_t type = UINT32_MAX;
                for (uint32_t j = 0; j < memory.memoryTypeCount; ++j)
                    if ((req.memoryTypeBits & (1u << j)) &&
                        (memory.memoryTypes[j].propertyFlags &
                         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                        type = j;
                        break;
                    }
                if (type == UINT32_MAX) throw std::runtime_error("No coherent host-visible Vulkan memory");
                VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ma.allocationSize  = req.size;
                ma.memoryTypeIndex = type;
                VkDeviceMemory m;
                check(vkAllocateMemory(device, &ma, nullptr, &m));
                r.memories.push_back(m);
                check(vkBindBufferMemory(device, b, m, 0));
                void* mapped;
                check(vkMapMemory(device, m, 0, bc.size, 0, &mapped));
                std::memset(mapped, 0, bc.size);
                if (i + 1 < n && bytes) std::memcpy(mapped, k.inputs[i].data(), bytes);
                vkUnmapMemory(device, m);
                bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
                infos[i]    = {b, 0, bc.size};
            }
            auto compiler = shaderc_compiler_initialize();
            if (!compiler) throw std::runtime_error("shaderc initialization failed");
            auto compiled = shaderc_compile_into_spv(compiler, k.source.data(), k.source.size(), shaderc_compute_shader,
                                                     "onnx.comp", "main", nullptr);
            shaderc_compiler_release(compiler);
            if (!compiled) throw std::runtime_error("shaderc compilation failed");
            if (shaderc_result_get_compilation_status(compiled) != shaderc_compilation_status_success) {
                std::string message = shaderc_result_get_error_message(compiled);
                shaderc_result_release(compiled);
                throw std::runtime_error(message);
            }
            std::vector<uint32_t> spv(shaderc_result_get_length(compiled) / 4);
            std::memcpy(spv.data(), shaderc_result_get_bytes(compiled), spv.size() * 4);
            shaderc_result_release(compiled);
            VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            sm.codeSize = spv.size() * 4;
            sm.pCode    = spv.data();
            check(vkCreateShaderModule(device, &sm, nullptr, &r.shader));
            VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            sl.bindingCount = n;
            sl.pBindings    = bindings.data();
            check(vkCreateDescriptorSetLayout(device, &sl, nullptr, &r.setLayout));
            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl.setLayoutCount = 1;
            pl.pSetLayouts    = &r.setLayout;
            check(vkCreatePipelineLayout(device, &pl, nullptr, &r.layout));
            VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            cp.layout       = r.layout;
            cp.stage        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            cp.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            cp.stage.module = r.shader;
            cp.stage.pName  = "main";
            check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &r.pipeline));
            VkDescriptorPoolSize       ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, n};
            VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dp.maxSets       = 1;
            dp.poolSizeCount = 1;
            dp.pPoolSizes    = &ps;
            check(vkCreateDescriptorPool(device, &dp, nullptr, &r.descriptors));
            VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            da.descriptorPool     = r.descriptors;
            da.descriptorSetCount = 1;
            da.pSetLayouts        = &r.setLayout;
            VkDescriptorSet set;
            check(vkAllocateDescriptorSets(device, &da, &set));
            std::vector<VkWriteDescriptorSet> writes(n);
            for (uint32_t i = 0; i < n; ++i) {
                writes[i]                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[i].dstSet          = set;
                writes[i].dstBinding      = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo     = &infos[i];
            }
            vkUpdateDescriptorSets(device, n, writes.data(), 0, nullptr);
            VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ca.commandPool        = commands;
            ca.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ca.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &ca, &r.command));
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(r.command, &begin));
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(r.command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(r.command, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
            vkCmdBindDescriptorSets(r.command, VK_PIPELINE_BIND_POINT_COMPUTE, r.layout, 0, 1, &set, 0, nullptr);
            vkCmdDispatch(r.command, (k.workItems + 63) / 64, 1, 1);
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(r.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);
            check(vkEndCommandBuffer(r.command));
            VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(vkCreateFence(device, &fc, nullptr, &r.fence));
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers    = &r.command;
            check(vkQueueSubmit(queue, 1, &submit, r.fence));
            check(vkWaitForFences(device, 1, &r.fence, VK_TRUE, UINT64_MAX));
            std::vector<uint8_t> result(k.outputBytes);
            void*                mapped;
            check(vkMapMemory(device, r.memories.back(), 0, k.outputBytes, 0, &mapped));
            std::memcpy(result.data(), mapped, k.outputBytes);
            vkUnmapMemory(device, r.memories.back());
            if (++dispatches % 100 == 0) std::cout << "GPU_DISPATCHES " << dispatches << std::endl;
            return eve::Result<std::vector<uint8_t>>::success(std::move(result));
        } catch (const std::exception& e) {
            return eve::Result<std::vector<uint8_t>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, e.what()));
        }
    }
};
}  // namespace
eve::Result<std::unique_ptr<eve::tensor::OnnxCompute>> createVulkanProbeCompute() {
    try {
        auto p = std::make_unique<VulkanCompute>();
        p->initialize();
        return eve::Result<std::unique_ptr<eve::tensor::OnnxCompute>>::success(std::move(p));
    } catch (const std::exception& e) {
        return eve::Result<std::unique_ptr<eve::tensor::OnnxCompute>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, e.what()));
    }
}
