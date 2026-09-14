#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/SolidPipeline.h"
#include "graphics/shaders/color_vert_spv.inc"
#include "graphics/shaders/color_frag_spv.inc"
#include "graphics/vulkan/GraphicsInternal.h"
#include "graphics/shaders/textured_vert_spv.inc"
#include "graphics/shaders/textured_frag_spv.inc"
#include "graphics/shaders/lit2d_vert_spv.inc"
#include "graphics/shaders/lit2d_frag_spv.inc"
namespace eve::graphics::vulkan {
vk::Pipeline createSolidColorPipeline(vkb::Device &device, const vkb::BuiltRenderPass &renderPass,
                                      vk::PipelineLayout layout,
                                      BlendMode mode) {
    if (mode == BlendMode::Additive || mode == BlendMode::Premultiplied ||
        mode == BlendMode::Multiply) {
        // cbs/attachments must outlive build(); the builder must stay a single
        // expression — copying the builder into a named local leaves its
        // shader-stage pName pointers dangling into the temporary's storage.
        std::vector<vk::PipelineColorBlendAttachmentState> attachments(1,
                                                                       makeBlendAttachment(mode));
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.logicOpEnable = false;
        cbs.attachmentCount = 1;
        cbs.pAttachments = attachments.data();
        return device.createPipeline()
            .useClassicPipeline(embeddedSpirv(color_vert_spv), embeddedSpirv(color_frag_spv))
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<ColorVertex>()
                                     .addAttributeDescription<ColorVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
            .setColorBlending(cbs)
            .build(renderPass);
    }
    if (mode == BlendMode::Alpha) {
        return device.createPipeline()
            .useClassicPipeline(embeddedSpirv(color_vert_spv), embeddedSpirv(color_frag_spv))
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<ColorVertex>()
                                     .addAttributeDescription<ColorVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
            .setAlphaBlending(1)
            .build(renderPass);
    }
    // Opaque: keep the original single-expression chain (no explicit blend state).
    return device.createPipeline()
        .useClassicPipeline(embeddedSpirv(color_vert_spv), embeddedSpirv(color_frag_spv))
        .setPipelineLayout(layout)
        .setVertexInputState(vkb::VertexInputStateBuilder()
                                 .addInputBinding<ColorVertex>()
                                 .addAttributeDescription<ColorVertex>())
        .setDynamicStatesViewportScissor()
        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                       vk::FrontFace::eCounterClockwise)
        .build(renderPass);
}


namespace {
vkb::BuiltRenderPass persistentColorPass(vkb::Device& device, vk::Format format) {
    vk::AttachmentDescription color{};
    color.format = format;
    color.samples = vk::SampleCountFlagBits::e1;
    color.loadOp = vk::AttachmentLoadOp::eLoad;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    auto pass = device.createRenderPass().addAttachment(color)
        .addSubpass(vkb::SubpassBuilder().addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal))
        .addDependency(VK_SUBPASS_EXTERNAL, 0,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite)
        .addDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eShaderRead).build();
    // addAttachment preserves the explicit initial layout; supply its role metadata.
    pass.colorAttachmentCount = 1;
    pass.colorsSampledAfter = true;
    pass.colorFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return pass;
}
} // namespace
void Graphics::ensureOffscreenPipelines() {
    if (offscreenRenderPass) return;

    offscreenRenderPass = persistentColorPass(device, vk::Format::eR8G8B8A8Unorm);

    offscreenSolidPipeline =
        createSolidColorPipeline(device, offscreenRenderPass, pipelineLayout);
    offscreenSolidAlphaPipeline =
        createSolidColorPipeline(device, offscreenRenderPass, pipelineLayout, BlendMode::Alpha);
    offscreenAdditiveSolidPipeline =
        createSolidColorPipeline(device, offscreenRenderPass, pipelineLayout, BlendMode::Additive);
    offscreenPremultipliedSolidPipeline = createSolidColorPipeline(
        device, offscreenRenderPass, pipelineLayout, BlendMode::Premultiplied);
    offscreenMultiplySolidPipeline = createSolidColorPipeline(
        device, offscreenRenderPass, pipelineLayout, BlendMode::Multiply);

    // Textured offscreen pipeline mirrors createTexturedPipeline but with offscreen RP.
    auto tvert = embeddedSpirv(textured_vert_spv);
    auto tfrag = embeddedSpirv(textured_frag_spv);
    offscreenTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout);
    offscreenAdditiveTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout,
                                    BlendMode::Additive);
    offscreenPremultipliedTexPipeline = createTexturedStylePipeline(
        tvert, tfrag, offscreenRenderPass, texPipelineLayout, BlendMode::Premultiplied);
    offscreenMultiplyTexPipeline = createTexturedStylePipeline(
        tvert, tfrag, offscreenRenderPass, texPipelineLayout, BlendMode::Multiply);
    offscreenOpaqueTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout,
                                    BlendMode::Opaque);

    if (lit2dPipelineLayout) {
        auto lvert = embeddedSpirv(lit2d_vert_spv);
        auto lfrag = embeddedSpirv(lit2d_frag_spv);
        offscreenLitPipeline =
            createTexturedStylePipeline(lvert, lfrag, offscreenRenderPass, lit2dPipelineLayout);
    }

    // Lazily create offscreen pipelines for any custom shaders already loaded.
    for (auto &sh : ownedShaders) ensureShaderOffscreenPipeline(sh.get());
}

void Graphics::ensureShaderOffscreenPipeline(Shader *shader) {
    if (!shader || !shader->gpuHandle || !offscreenRenderPass) return;
    // Mesh3D SPIR-V / layout is incompatible with the 2D textured offscreen pass.
    if (shader->getKind() == Shader::Kind::eMesh3D) return;
    auto *gpu = static_cast<GpuShader *>(shader->gpuHandle);
    if (gpu->isMesh3D || gpu->offscreenPipeline) return;
    gpu->offscreenPipeline = createTexturedStylePipeline(shader->vertexSpirv(), shader->fragmentSpirv(),
                                                         offscreenRenderPass, shaderPipelineLayout);
    gpu->offscreenOpaquePipeline = createTexturedStylePipeline(
        shader->vertexSpirv(), shader->fragmentSpirv(), offscreenRenderPass,
        shaderPipelineLayout, BlendMode::Opaque);
}

void Graphics::ensureHdrOffscreenPipelines() {
    if (hdrOffscreenRenderPass) return;
    hdrOffscreenRenderPass =
        device.createRenderPass()
            .addSampledColorAttachment(vk::Format::eR16G16B16A16Sfloat)
            .addSubpass(vkb::SubpassBuilder().addAttachmentRef(
                0, vk::ImageLayout::eColorAttachmentOptimal))
            .addExternalShaderReadDependencies()
            .build();

    auto vert = embeddedSpirv(textured_vert_spv);
    auto frag = embeddedSpirv(textured_frag_spv);
    hdrOffscreenTexPipeline = createTexturedStylePipeline(
        vert, frag, hdrOffscreenRenderPass, texPipelineLayout);
    hdrOffscreenOpaqueTexPipeline = createTexturedStylePipeline(
        vert, frag, hdrOffscreenRenderPass, texPipelineLayout, BlendMode::Opaque);
    for (auto &shader : ownedShaders) ensureShaderHdrOffscreenPipeline(shader.get());
}

void Graphics::ensureShaderHdrOffscreenPipeline(Shader *shader) {
    if (!shader || !shader->gpuHandle || !hdrOffscreenRenderPass) return;
    if (shader->getKind() == Shader::Kind::eMesh3D) return;
    auto *gpu = static_cast<GpuShader *>(shader->gpuHandle);
    if (gpu->isMesh3D || gpu->hdrOffscreenPipeline) return;
    gpu->hdrOffscreenPipeline = createTexturedStylePipeline(
        shader->vertexSpirv(), shader->fragmentSpirv(), hdrOffscreenRenderPass,
        shaderPipelineLayout);
    gpu->hdrOffscreenOpaquePipeline = createTexturedStylePipeline(
        shader->vertexSpirv(), shader->fragmentSpirv(), hdrOffscreenRenderPass,
        shaderPipelineLayout, BlendMode::Opaque);
}


} // namespace eve::graphics::vulkan
