#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION           1000000
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "vk_mem_alloc.h"
#define VK(call)                                                                                            \
    do {                                                                                                    \
        VkResult r = (call);                                                                                \
        if (r != VK_SUCCESS) throw std::runtime_error(std::string(#call) + " result=" + std::to_string(r)); \
    } while (0)
VkPhysicalDevice                 physical;
VkDevice                         device;
VkQueue                          queue;
VkCommandPool                    pool;
VkPhysicalDeviceMemoryProperties memoryProperties;
VkPhysicalDeviceProperties       properties;
VmaAllocator                     vmaAllocator{};
bool                             useVma{};
bool                             depthMode{}, dynamicViewport{};
VkBuffer borrowedVertex{}, borrowedIndex{};
#ifdef PROBE_EMBEDDED
extern "C" VkPipeline wrapperPipeline(const VkGraphicsPipelineCreateInfo*);
bool useWrapperPipeline{};
#endif
VkDeviceMemory                   sharedMemory{};
VkDeviceSize                     sharedCursor{}, sharedBytes{};
void*                            sharedMapped{};
bool                             flushCoherent{};
uint32_t                         memoryType(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("memory type unavailable");
}
struct Buffer {
    VmaAllocation  allocation{};
    VkBuffer       handle{};
    VkDeviceMemory memory{};
    VkDeviceSize   offset{}, size{};
};
Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool offset) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size  = size;
    ci.usage = usage;
    if (useVma) {
        VmaAllocationCreateInfo ai{};
        ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VmaAllocationInfo info{};
        VK(vmaCreateBuffer(vmaAllocator, &ci, &ai, &b.handle, &b.allocation, &info));
        b.memory = info.deviceMemory;
        return b;
    }
    VK(vkCreateBuffer(device, &ci, nullptr, &b.handle));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, b.handle, &mr);
    b.offset = offset ? ((4096 + mr.alignment - 1) / mr.alignment) * mr.alignment : 0;
    if (sharedBytes) {
        if (!sharedMemory) {
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = sharedBytes;
            ai.memoryTypeIndex = memoryType(mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK(vkAllocateMemory(device, &ai, nullptr, &sharedMemory));
            VK(vkMapMemory(device, sharedMemory, 0, VK_WHOLE_SIZE, 0, &sharedMapped));
        }
        b.offset     = ((sharedCursor + mr.alignment - 1) / mr.alignment) * mr.alignment;
        sharedCursor = b.offset + mr.size;
        if (sharedCursor > sharedBytes) throw std::runtime_error("probe shared pool exhausted");
        b.memory = sharedMemory;
        VK(vkBindBufferMemory(device, b.handle, b.memory, b.offset));
        return b;
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size + b.offset;
    ai.memoryTypeIndex =
        memoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(device, &ai, nullptr, &b.memory));
    VK(vkBindBufferMemory(device, b.handle, b.memory, b.offset));
    return b;
}
void write(Buffer b, const void* p, size_t bytes, VkDeviceSize extra = 0) {
    if (b.allocation) {
        void* ptr;
        VK(vmaMapMemory(vmaAllocator, b.allocation, &ptr));
        std::memcpy((char*)ptr + extra, p, bytes);
        vmaUnmapMemory(vmaAllocator, b.allocation);
        return;
    }

    if (b.memory == sharedMemory && sharedMapped) {
        std::memcpy((char*)sharedMapped + b.offset + extra, p, bytes);
        if (flushCoherent) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = b.memory;
            range.size   = VK_WHOLE_SIZE;
            VK(vkFlushMappedMemoryRanges(device, 1, &range));
        }
        return;
    }
    void* dst;
    VK(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &dst));
    std::memcpy((char*)dst + b.offset + extra, p, bytes);
    vkUnmapMemory(device, b.memory);
}
void release(Buffer b) {
    if (b.allocation) {
        vmaDestroyBuffer(vmaAllocator, b.handle, b.allocation);
        return;
    }

    vkDestroyBuffer(device, b.handle, nullptr);
    if (b.memory != sharedMemory) vkFreeMemory(device, b.memory, nullptr);
}
VkShaderModule shader(const std::string& name) {
    std::ifstream f(name, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(name);
    size_t                n = size_t(f.tellg());
    std::vector<uint32_t> w(n / 4);
    f.seekg(0);
    f.read((char*)w.data(), n);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = n;
    ci.pCode    = w.data();
    VkShaderModule s;
    VK(vkCreateShaderModule(device, &ci, nullptr, &s));
    return s;
}
struct Vertex {
    float    p[3], n[3], uv[2];
    uint16_t joints[4];
    float    weights[4], color[4], tangent[4];
};
bool render(const char* mode, bool full, bool offset, int indices) {
    const bool  gbuffer   = std::string(mode).starts_with("GBUFFER");
    const bool  generated = std::strcmp(mode, "GENERATED") == 0;
    const bool  dynamic   = !gbuffer && (full || std::strcmp(mode, "UBO") == 0);
    std::string label     = std::string(mode) + " layout=" + (full ? "engine" : "minimal") +
                        " memoryOffset=" + std::to_string(offset) + " indices=" + std::to_string(indices) +
                        " sharedBytes=" + std::to_string(sharedBytes) + " flush=" + std::to_string(flushCoherent) +
                        " VMA=" + std::to_string(useVma) + " depth=" + std::to_string(depthMode) +
                        " dynamicViewport=" + std::to_string(dynamicViewport) + " borrowed=" + std::to_string(bool(borrowedVertex));
    const int                   attachments = gbuffer || std::strcmp(mode, "MRT") == 0 ? 5 : 1;
    std::vector<VkImage>        images(attachments);
    std::vector<VkImageView>    views(attachments);
    std::vector<VkDeviceMemory> imageMemory(attachments);
    for (int i = 0; i < attachments; ++i) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent        = {64, 64, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK(vkCreateImage(device, &ci, nullptr, &images[i]));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, images[i], &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = memoryType(mr.memoryTypeBits, 0);
        VK(vkAllocateMemory(device, &ai, nullptr, &imageMemory[i]));
        VK(vkBindImageMemory(device, images[i], imageMemory[i], 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image            = images[i];
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = ci.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(device, &vi, nullptr, &views[i]));
    }
    VkImage        depthImage{};
    VkDeviceMemory depthMemory{};
    VkImageView    depthView{};
    if (depthMode) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType   = VK_IMAGE_TYPE_2D;
        ci.format      = VK_FORMAT_D32_SFLOAT;
        ci.extent      = {64, 64, 1};
        ci.mipLevels   = 1;
        ci.arrayLayers = 1;
        ci.samples     = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling      = VK_IMAGE_TILING_OPTIMAL;
        ci.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK(vkCreateImage(device, &ci, nullptr, &depthImage));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, depthImage, &mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = memoryType(mr.memoryTypeBits, 0);
        VK(vkAllocateMemory(device, &ai, nullptr, &depthMemory));
        VK(vkBindImageMemory(device, depthImage, depthMemory, 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image            = depthImage;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = ci.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VK(vkCreateImageView(device, &vi, nullptr, &depthView));
        views.push_back(depthView);
    }
    std::vector<VkAttachmentDescription> ads(attachments);
    std::vector<VkAttachmentReference>   refs(attachments);
    for (int i = 0; i < attachments; ++i) {
        ads[i].format        = VK_FORMAT_R8G8B8A8_UNORM;
        ads[i].samples       = VK_SAMPLE_COUNT_1_BIT;
        ads[i].loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ads[i].storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
        ads[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ads[i].finalLayout   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        refs[i]              = {uint32_t(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    VkAttachmentReference depthRef{uint32_t(attachments), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (depthMode) {
        VkAttachmentDescription ad{};
        ad.format        = VK_FORMAT_D32_SFLOAT;
        ad.samples       = VK_SAMPLE_COUNT_1_BIT;
        ad.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ad.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
        ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ad.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ads.push_back(ad);
    }
    VkSubpassDescription sp{};
    sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = attachments;
    sp.pColorAttachments    = refs.data();
    if (depthMode) sp.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dep{};
    dep.srcSubpass    = 0;
    dep.dstSubpass    = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = uint32_t(ads.size());
    ri.pAttachments    = ads.data();
    ri.subpassCount    = 1;
    ri.pSubpasses      = &sp;
    ri.dependencyCount = 1;
    ri.pDependencies   = &dep;
    VkRenderPass pass;
    VK(vkCreateRenderPass(device, &ri, nullptr, &pass));
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass      = pass;
    fi.attachmentCount = uint32_t(views.size());
    fi.pAttachments    = views.data();
    fi.width           = 64;
    fi.height          = 64;
    fi.layers          = 1;
    VkFramebuffer framebuffer;
    VK(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer));
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    if (full) bindings.push_back({21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
    bindings.push_back({0, dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    if (full) {
        for (uint32_t binding : {1u, 2u, 3u, 5u, 6u, 7u, 8u, 9u, 10u, 16u, 17u, 18u, 20u})
            bindings.push_back({binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                VkShaderStageFlags((binding == 1 || binding == 6)
                                                       ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                                       : VK_SHADER_STAGE_FRAGMENT_BIT),
                                nullptr});
        bindings.push_back({4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    } else
        bindings.push_back({21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
    if (!full)
        bindings.push_back({1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    if (gbuffer) {
        bindings.clear();
        bindings.push_back({0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        bindings.push_back({1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = bindings.size();
    li.pBindings    = bindings.data();
    VkDescriptorSetLayout layout;
    VK(vkCreateDescriptorSetLayout(device, &li, nullptr, &layout));
    VkPushConstantRange        range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &layout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &range;
    VkPipelineLayout pipelineLayout;
    VK(vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout));
    VkShaderModule vs = shader(std::string(mode) + ".vert.spv"), fs = shader(std::string(mode) + ".frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage  = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName  = "main";
    }
    VkVertexInputBindingDescription   vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},     {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},       {3, 0, VK_FORMAT_R16G16B16A16_UINT, 32},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40}, {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 56},
        {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 72}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (!generated) {
        vertex.vertexBindingDescriptionCount   = 1;
        vertex.pVertexBindingDescriptions      = &vb;
        vertex.vertexAttributeDescriptionCount = full ? 7 : (gbuffer ? 3 : 1);
        vertex.pVertexAttributeDescriptions    = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport                        viewport{0, 0, 64, 64, 0, 1};
    VkRect2D                          scissor{{0, 0}, {64, 64}};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.pViewports    = &viewport;
    vp.scissorCount  = 1;
    vp.pScissors     = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    std::vector<VkPipelineColorBlendAttachmentState> blend(attachments);
    for (auto& b : blend) b.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = attachments;
    bs.pAttachments    = blend.data();
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable                               = VK_TRUE;
    ds.depthWriteEnable                              = VK_TRUE;
    ds.depthCompareOp                                = VK_COMPARE_OP_LESS;
    VkDynamicState                   dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynamicStates;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vertex;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vp;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState   = &ms;
    pi.pColorBlendState    = &bs;
    pi.layout              = pipelineLayout;
    pi.renderPass          = pass;
    if (depthMode) pi.pDepthStencilState = &ds;
    if (dynamicViewport) pi.pDynamicState = &dyn;
    VkPipeline pipeline;
    #ifdef PROBE_EMBEDDED
    if (useWrapperPipeline) pipeline=wrapperPipeline(&pi);
    else
#endif
    VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline));
    std::array<Vertex, 4> vertices{};
    vertices[0].p[0] = -1;
    vertices[0].p[1] = -1;
    vertices[1].p[0] = 3;
    vertices[1].p[1] = -1;
    vertices[2].p[0] = -1;
    vertices[2].p[1] = 3;
    vertices[3]=vertices[0];
    for (auto& v : vertices) {
        v.p[2]  = .5f;
        v.n[2]  = 1.0f;
        v.uv[0] = v.uv[1] = 0.5f;
    }
    Buffer vbo = buffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, offset);
    write(vbo, vertices.data(), sizeof(vertices));
    uint32_t ix32[]{0, 1, 2};
    uint16_t ix16[]{0, 1, 2};
    Buffer   ibo = buffer(12, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, offset);
    write(ibo, indices == 16 ? (void*)ix16 : (void*)ix32, indices == 16 ? 6 : 12);
    std::array<float, 32> constants{};
    constants[0] = constants[5] = constants[10] = constants[15] = 1;
    constants[16] = constants[21] = constants[26] = 1;
    constants[28] = constants[31] = 1;
    if (gbuffer) {
        constants[28]   = 0.1f;
        constants[29]   = 100.0f;
        uint32_t packed = 63u | (32u << 18u) | (95u << 25u);
        std::memcpy(&constants[30], &packed, 4);
        constants[31] = float(2047u | (2047u << 12u));
    }
    VkDeviceSize          uboOffset = dynamic ? properties.limits.minUniformBufferOffsetAlignment : 0;
    std::array<float, 20> uniforms{};
    uniforms[0] = uniforms[5] = uniforms[10] = uniforms[15] = uniforms[16] = uniforms[19] = 1;
    Buffer ubo = buffer(sizeof(uniforms) + uboOffset, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, offset);
    write(ubo, uniforms.data(), sizeof(uniforms), uboOffset);
    Buffer ssbo = buffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, offset);
    write(ssbo, constants.data(), 64);
    VkDescriptorPoolSize       sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
                                          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2},
                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 13}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets       = 1;
    dpi.poolSizeCount = 4;
    dpi.pPoolSizes    = sizes;
    VkDescriptorPool descriptorPool;
    VK(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptorPool));
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool     = descriptorPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &layout;
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(device, &dai, &set));
    VkImage           texture;
    VkDeviceMemory    textureMemory;
    VkImageView       textureView;
    VkSampler         sampler;
    VkImageCreateInfo ti{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ti.imageType   = VK_IMAGE_TYPE_2D;
    ti.format      = VK_FORMAT_R8G8B8A8_UNORM;
    ti.extent      = {1, 1, 1};
    ti.mipLevels   = 1;
    ti.arrayLayers = 1;
    ti.samples     = VK_SAMPLE_COUNT_1_BIT;
    ti.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ti.usage       = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VK(vkCreateImage(device, &ti, nullptr, &texture));
    VkMemoryRequirements tm;
    vkGetImageMemoryRequirements(device, texture, &tm);
    VkMemoryAllocateInfo tai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    tai.allocationSize  = tm.size;
    tai.memoryTypeIndex = memoryType(tm.memoryTypeBits, 0);
    VK(vkAllocateMemory(device, &tai, nullptr, &textureMemory));
    VK(vkBindImageMemory(device, texture, textureMemory, 0));
    VkImageViewCreateInfo tvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    tvi.image            = texture;
    tvi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    tvi.format           = ti.format;
    tvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(device, &tvi, nullptr, &textureView));
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.maxLod    = 1;
    VK(vkCreateSampler(device, &sci, nullptr, &sampler));
    Buffer  textureStaging = buffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, offset);
    uint8_t red[]          = {255, 0, 0, 255};
    write(textureStaging, red, 4);
    VkDescriptorBufferInfo            ub{ubo.handle, 0, sizeof(uniforms)}, sb{ssbo.handle, 0, 64};
    std::vector<VkWriteDescriptorSet> writes;
    if (!gbuffer) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet          = set;
        w.dstBinding      = 0;
        w.descriptorCount = 1;
        w.descriptorType  = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo     = &ub;
        writes.push_back(w);
        w.dstBinding     = 21;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo    = &sb;
        writes.push_back(w);
        if (full) {
            w.dstBinding     = 4;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            w.pBufferInfo    = &ub;
            writes.push_back(w);
        }
    }
    VkDescriptorImageInfo sampled{sampler, textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (uint32_t binding : {0u, 1u, 2u, 3u, 5u, 6u, 7u, 8u, 9u, 10u, 16u, 17u, 18u, 20u}) {
        if (gbuffer) {
            if (binding > 1) continue;
        } else {
            if (binding == 0) continue;
            if (!full && binding != 1) continue;
        }
        VkWriteDescriptorSet iw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        iw.dstSet          = set;
        iw.dstBinding      = binding;
        iw.descriptorCount = 1;
        iw.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        iw.pImageInfo      = &sampled;
        writes.push_back(iw);
    }
    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    Buffer                      readback = buffer(64 * 64 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, offset);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool        = pool;
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK(vkAllocateCommandBuffers(device, &cai, &cb));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(cb, &bi));
    VkImageMemoryBarrier tb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    tb.image               = texture;
    tb.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    tb.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    tb.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tb.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    tb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &tb);
    VkBufferImageCopy texCopy{};
    texCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    texCopy.imageExtent      = {1, 1, 1};
    vkCmdCopyBufferToImage(cb, textureStaging.handle, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &texCopy);
    tb.oldLayout                = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tb.newLayout                = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tb.srcAccessMask            = VK_ACCESS_TRANSFER_WRITE_BIT;
    tb.dstAccessMask            = VK_ACCESS_SHADER_READ_BIT;
    VkPipelineStageFlags target = (std::strcmp(mode, "SAMPLER_VERTEX") == 0 || std::strcmp(mode, "GBUFFER_VERTEX") == 0)
                                      ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                      : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, target, 0, 0, nullptr, 0, nullptr, 1, &tb);
    std::vector<VkClearValue> clears(attachments + int(depthMode));
    for (auto& c : clears) c.color.float32[3] = 1;
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass  = pass;
    rbi.framebuffer = framebuffer;
    rbi.renderArea  = {{0, 0}, {64, 64}};
    if (depthMode) clears.back().depthStencil = {1.0f, 0};
    rbi.clearValueCount = uint32_t(clears.size());
    rbi.pClearValues    = clears.data();
    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (dynamicViewport) {
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);
    }
    uint32_t dynamicOffsets[2] = {uint32_t(uboOffset), 0};
    if (!dynamic) dynamicOffsets[0] = 0;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set,
                            gbuffer ? 0 : ((dynamic ? 1 : 0) + (full ? 1 : 0)), dynamicOffsets);
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128,
                       constants.data());
    VkDeviceSize zero = 0;
    VkBuffer drawVertex=borrowedVertex ? borrowedVertex : vbo.handle;
    if (!generated) vkCmdBindVertexBuffers(cb, 0, 1, &drawVertex, &zero);
    if (!generated && indices) {
        vkCmdBindIndexBuffer(cb, borrowedIndex ? borrowedIndex : ibo.handle, 0, indices == 16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, borrowedIndex ? 6 : 3, 1, 0, 0, 0);
    } else
        vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent      = {64, 64, 1};
    vkCmdCopyImageToBuffer(cb, images[gbuffer ? 2 : 0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1,
                           &copy);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
                         nullptr);
    VK(vkEndCommandBuffer(cb));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence           fence;
    VK(vkCreateFence(device, &fci, nullptr, &fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cb;
    VK(vkQueueSubmit(queue, 1, &submit, fence));
    VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    void* bytes = sharedMapped;
    if (readback.allocation) {
        VK(vmaMapMemory(vmaAllocator, readback.allocation, &bytes));
    }

    if (readback.allocation) {
    } else if (sharedMapped) {
        if (flushCoherent) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = sharedMemory;
            range.size   = VK_WHOLE_SIZE;
            VK(vkInvalidateMappedMemoryRanges(device, 1, &range));
        }
    } else
        VK(vkMapMemory(device, readback.memory, 0, VK_WHOLE_SIZE, 0, &bytes));
    const uint8_t* p    = (uint8_t*)bytes + readback.offset + (20 * 64 + 40) * 4;
    bool           good = p[0] > 240 && p[1] < 10 && p[2] < 10;
    std::printf("PROBE %s center=%u,%u,%u,%u %s\n", label.c_str(), p[0], p[1], p[2], p[3], good ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (readback.allocation)
        vmaUnmapMemory(vmaAllocator, readback.allocation);
    else if (!sharedMapped)
        vkUnmapMemory(device, readback.memory);
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cb);
    release(textureStaging);
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyImageView(device, textureView, nullptr);
    vkDestroyImage(device, texture, nullptr);
    vkFreeMemory(device, textureMemory, nullptr);
    release(readback);
    release(vbo);
    release(ibo);
    release(ubo);
    release(ssbo);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, pass, nullptr);
    for (int i = 0; i < attachments; ++i) {
        vkDestroyImageView(device, views[i], nullptr);
        vkDestroyImage(device, images[i], nullptr);
        vkFreeMemory(device, imageMemory[i], nullptr);
    }
    if (depthMode) {
        vkDestroyImageView(device, depthView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthMemory, nullptr);
    }
    if (sharedMemory) {
        vkUnmapMemory(device, sharedMemory);
        vkFreeMemory(device, sharedMemory, nullptr);
        sharedMemory = VK_NULL_HANDLE;
        sharedMapped = nullptr;
        sharedCursor = 0;
    }
    return good;
}
#ifdef PROBE_EMBEDDED
extern "C" int engineRawProbe(VkPhysicalDevice pd, VkDevice dev, uint32_t family, VkBuffer bv, VkBuffer bi, VmaAllocator allocator) {
    physical=pd; device=dev;
    vkGetPhysicalDeviceProperties(physical, &properties);
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex=family; pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK(vkCreateCommandPool(device,&pi,nullptr,&pool));
    int failures=0;
    useVma=false; sharedBytes=0;
    for (bool depth : {false,true}) {
        depthMode=depth; dynamicViewport=true;
        for (const char* mode : {"GENERATED","VERTEX","PUSH","UBO","GBUFFER_EMBEDDED"})
            failures+=!render(mode,false,false,32);
    }
    borrowedVertex=bv; borrowedIndex=bi;
    for (bool depth : {false,true}) {
        depthMode=depth;
        for (const char* mode : {"VERTEX","PUSH","GBUFFER_EMBEDDED"})
            failures+=!render(mode,false,false,32);
    }
    borrowedVertex=borrowedIndex=VK_NULL_HANDLE;
    for (int which : {0,1}) {
        borrowedVertex=which==0 ? bv : VK_NULL_HANDLE;
        borrowedIndex=which==1 ? bi : VK_NULL_HANDLE;
        std::printf("ENGINE_BORROW_ONLY %s\n",which==0 ? "vertex" : "index");
        depthMode=false;
        failures+=!render("VERTEX",false,false,32);
    }
    borrowedVertex=borrowedIndex=VK_NULL_HANDLE;
    useWrapperPipeline=true;
    std::puts("ENGINE_WRAPPER_PIPELINE_START");
    for (bool depth : {false,true}) {
        depthMode=depth;
        for (const char* mode : {"GENERATED","VERTEX","PUSH","GBUFFER_EMBEDDED"})
            failures+=!render(mode,false,false,32);
    }
    useWrapperPipeline=false;
    vmaAllocator=allocator; useVma=true;
    std::puts("ENGINE_ALLOCATOR_FRESH_START");
    for (const char* mode : {"VERTEX","PUSH","GBUFFER_EMBEDDED"})
        failures+=!render(mode,false,false,32);
    useVma=false;
    vkDestroyCommandPool(device,pool,nullptr);
    return failures;
}
#else
int main() {
    try {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName           = "PR701 independent Vulkan probe";
        app.apiVersion                 = VK_API_VERSION_1_2;
        const char*          extension = "VK_KHR_portability_enumeration";
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo        = &app;
        ci.flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        ci.enabledExtensionCount   = 1;
        ci.ppEnabledExtensionNames = &extension;
        VkInstance instance;
        VK(vkCreateInstance(&ci, nullptr, &instance));
        uint32_t n = 0;
        VK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
        if (!n) throw std::runtime_error("no devices");
        std::vector<VkPhysicalDevice> devices(n);
        VK(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
        physical = devices[0];
        vkGetPhysicalDeviceProperties(physical, &properties);
        vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
        std::printf(
            "DEVICE %s api=%u driver=%u samplers=%u uniformsAlign=%zu maxPush=%u maxVertexStride=%u maxColor=%u\n",
            properties.deviceName, properties.apiVersion, properties.driverVersion,
            properties.limits.maxPerStageDescriptorSamplers, size_t(properties.limits.minUniformBufferOffsetAlignment),
            properties.limits.maxPushConstantsSize, properties.limits.maxVertexInputBindingStride,
            properties.limits.maxColorAttachments);
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> q(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &qn, q.data());
        uint32_t family = 0;
        while (family < qn && !(q[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
        float                   priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex            = family;
        qi.queueCount                  = 1;
        qi.pQueuePriorities            = &priority;
        const char*        portability = "VK_KHR_portability_subset";
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount    = 1;
        di.pQueueCreateInfos       = &qi;
        di.enabledExtensionCount   = 1;
        di.ppEnabledExtensionNames = &portability;
        VK(vkCreateDevice(physical, &di, nullptr, &device));
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family;
        pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK(vkCreateCommandPool(device, &pi, nullptr, &pool));
        VmaVulkanFunctions funcs{};
        funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        funcs.vkGetDeviceProcAddr =
            reinterpret_cast<PFN_vkGetDeviceProcAddr>(vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
        VmaAllocatorCreateInfo vci{};
        vci.instance         = instance;
        vci.physicalDevice   = physical;
        vci.device           = device;
        vci.vulkanApiVersion = VK_API_VERSION_1_0;
        vci.pVulkanFunctions = &funcs;
        VK(vmaCreateAllocator(&vci, &vmaAllocator));
        bool good = true;
        for (VkDeviceSize block : {VkDeviceSize(0), VkDeviceSize(1u << 20), VkDeviceSize(128u << 20)}) {
            sharedBytes = block;
            for (bool flush : {false, true}) {
                if (!block && flush) continue;
                flushCoherent = flush;
                for (bool full : {false, true})
                    for (bool offset : {false, true})
                        for (const char* mode : {"GENERATED", "VERTEX", "PUSH", "UBO", "SSBO", "MRT", "SAMPLER_VERTEX",
                                                 "SAMPLER_ALL", "GBUFFER", "GBUFFER_VERTEX", "GBUFFER_EMBEDDED"})
                            for (int indices : {0, 16, 32}) {
                                if (std::strcmp(mode, "VERTEX") && indices != 32) continue;
                                try {
                                    good = render(mode, full, offset, indices) && good;
                                } catch (const std::exception& e) {
                                    std::printf("PROBE ERROR %s full=%d offset=%d indices=%d %s\n", mode, full, offset,
                                                indices, e.what());
                                    good = false;
                                }
                            }
            }
        }
        useVma        = true;
        sharedBytes   = 0;
        flushCoherent = false;
        for (int persistent : {0, 8, 16, 32, 64}) {
            Buffer keeper{};
            if (persistent) {
                keeper = buffer(VkDeviceSize(persistent) << 20,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                false);
                void* ptr;
                VK(vmaMapMemory(vmaAllocator, keeper.allocation, &ptr));
            }
            std::printf("VMA persistent=%d\n", int(persistent));
            for (bool full : {false, true})
                for (const char* mode : {"VERTEX", "PUSH", "UBO", "SSBO", "SAMPLER_ALL", "GBUFFER_EMBEDDED"})
                    good = render(mode, full, false, 32) && good;
            if (persistent) {
                vmaUnmapMemory(vmaAllocator, keeper.allocation);
                release(keeper);
            }
        }
        useVma        = false;
        depthMode     = true;
        sharedBytes   = 0;
        flushCoherent = false;
        for (bool dynViewport : {false, true}) {
            dynamicViewport = dynViewport;
            for (bool full : {false, true})
                for (const char* mode : {"VERTEX", "PUSH", "UBO", "SAMPLER_ALL", "GBUFFER_EMBEDDED"})
                    good = render(mode, full, false, 32) && good;
        }
        vmaDestroyAllocator(vmaAllocator);
        VK(vkDeviceWaitIdle(device));
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return good ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "PROBE FATAL %s\n", e.what());
        return 2;
    }
}

#endif
