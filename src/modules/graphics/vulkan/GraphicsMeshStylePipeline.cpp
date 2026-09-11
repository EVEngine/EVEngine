#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
vk::Pipeline Graphics::createMesh3DStylePipeline(const std::vector<uint32_t>& vert, const std::vector<uint32_t>& frag,
                                                 vk::PipelineLayout layout, const vkb::BuiltRenderPass& rp,
                                                 vk::SampleCountFlagBits samples, BlendMode blend, bool depthWrite,
                                                 bool doubleSided, const MeshShaderRasterState& raster) {
    vk::ShaderModuleCreateInfo vertexInfo{};
    vertexInfo.codeSize                   = vert.size() * sizeof(uint32_t);
    vertexInfo.pCode                      = vert.data();
    auto                       vertModule = device->createShaderModuleUnique(vertexInfo);
    vk::ShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.codeSize   = frag.size() * sizeof(uint32_t);
    fragmentInfo.pCode      = frag.data();
    auto         fragModule = device->createShaderModuleUnique(fragmentInfo);
    const auto   cull       = doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
    const auto   compare    = raster.depthCompare == MeshDepthCompare::Always      ? vk::CompareOp::eAlways
                              : raster.depthCompare == MeshDepthCompare::LessEqual ? vk::CompareOp::eLessOrEqual
                                                                                   : vk::CompareOp::eLess;
    vk::Pipeline pipe{};
    if (blend != BlendMode::Opaque || raster.colorWriteMask != 15) {
        std::vector<vk::PipelineColorBlendAttachmentState> attachments(1, makeBlendAttachment(blend));
        attachments[0].blendEnable    = blend != BlendMode::Opaque;
        attachments[0].colorWriteMask = vk::ColorComponentFlags(raster.colorWriteMask);
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.attachmentCount = 1;
        cbs.pAttachments    = attachments.data();
        pipe =
            device.createPipeline()
                .useClassicPipeline(vertModule.get(), fragModule.get())
                .setPipelineLayout(layout)
                .setVertexInputState(
                    vkb::VertexInputStateBuilder().addInputBinding<MeshVertex>().addAttributeDescription<MeshVertex>())
                .setDynamicStatesViewportScissor()
                .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, cull, vk::FrontFace::eCounterClockwise)
                .setMultisampler(false, samples)
                .setDepthBias(raster.depthBiasConstant, raster.depthBiasSlope)
                .setDepthStencil(true, depthWrite, compare)
                .setColorBlending(cbs)
                .build(rp);
    } else {
        pipe =
            device.createPipeline()
                .useClassicPipeline(vertModule.get(), fragModule.get())
                .setPipelineLayout(layout)
                .setVertexInputState(
                    vkb::VertexInputStateBuilder().addInputBinding<MeshVertex>().addAttributeDescription<MeshVertex>())
                .setDynamicStatesViewportScissor()
                .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, cull, vk::FrontFace::eCounterClockwise)
                .setMultisampler(false, samples)
                .setDepthBias(raster.depthBiasConstant, raster.depthBiasSlope)
                .setDepthStencil(true, depthWrite, compare)
                .setColorAttachmentCount(1)
                .build(rp);
    }
    return pipe;
}

}  // namespace eve::graphics::vulkan
