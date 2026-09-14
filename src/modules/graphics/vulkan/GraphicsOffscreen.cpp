#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/vulkan/GraphicsInternal.h"
#include "common/Exception.h"
#include <array>
#include <vector>
namespace eve::graphics::vulkan {
void Graphics::flushToOffscreen(OffscreenCanvas *canvas) {
    auto solid = std::move(solidBatches);
    auto textured = std::move(texturedBatches);
    auto lit = std::move(litBatches);
    auto spans = std::move(overlaySpans);
    clear2DBatches();

    const Color cc = canvas->pendingClearColor();
    const bool needClear = canvas->takePendingClear();
    bool hasSolid = false;
    for (const auto &sb : solid)
        if (!sb.batch.empty()) hasSolid = true;
    if (!hasSolid && textured.empty() && lit.empty() && !needClear) return;

    // Offscreen color is a single shared image. An in-flight swapchain frame
    // may still be sampling it (draw canvas to screen last frame), so drain
    // those frames before transitioning it back to a color attachment.
    waitForSharedGpuResources();

    auto recordOffscreen = [&](vk::CommandBuffer cb) {
                                canvas->colorImage().setLayout(cb, vk::ImageLayout::eColorAttachmentOptimal);

                                vk::ClearValue cv{
                                    vk::ClearColorValue(std::array<float, 4>{cc.r, cc.g, cc.b, cc.a})};
                                vk::RenderPassBeginInfo rpBegin{};
                                rpBegin.renderPass = canvas->isHDR() ? hdrOffscreenRenderPass
                                                                    : offscreenRenderPass;
                                rpBegin.framebuffer = canvas->framebuffer();
                                rpBegin.renderArea.extent =
                                    vk::Extent2D{uint32_t(canvas->getWidth()), uint32_t(canvas->getHeight())};
                                rpBegin.clearValueCount = 1;
                                rpBegin.pClearValues = &cv;
                                canvas->colorImage().beginColorAttachment();
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
                                if (needClear) {
                                    vk::ClearAttachment attachment{};
                                    attachment.aspectMask = vk::ImageAspectFlagBits::eColor;
                                    attachment.colorAttachment = 0;
                                    attachment.clearValue = cv;
                                    vk::ClearRect rect{};
                                    rect.rect = rpBegin.renderArea;
                                    rect.layerCount = 1;
                                    cb.clearAttachments(attachment, rect);
                                }

                                setViewportAndScissor(cb, uint32_t(canvas->getWidth()),
                                                      uint32_t(canvas->getHeight()));

                                std::vector<vkb::HostVertexBuffer> &solidBufs =
                                    offscreenBuffers.solidBufs;
                                std::vector<vkb::HostVertexBuffer> &texBufs = offscreenBuffers.texBufs;
                                size_t texBufIndex = 0;
                                const int vw = canvas->getWidth();
                                const int vh = canvas->getHeight();

                                auto offscreenTexPipe = [&](BlendMode mode) -> vk::Pipeline {
                                    switch (mode) {
                                        case BlendMode::Additive:
                                            return offscreenAdditiveTexPipeline;
                                        case BlendMode::Premultiplied:
                                            return offscreenPremultipliedTexPipeline;
                                        case BlendMode::Multiply:
                                            return offscreenMultiplyTexPipeline;
                                        case BlendMode::Opaque:
                                            return canvas->isHDR() ? hdrOffscreenOpaqueTexPipeline
                                                                   : offscreenOpaqueTexPipeline;
                                        case BlendMode::Alpha:
                                        default:
                                            return canvas->isHDR() ? hdrOffscreenTexPipeline
                                                                   : offscreenTexPipeline;
                                    }
                                };
                                auto offscreenSolidPipe = [&](BlendMode mode) -> vk::Pipeline {
                                    switch (mode) {
                                        case BlendMode::Additive:
                                            return offscreenAdditiveSolidPipeline;
                                        case BlendMode::Premultiplied:
                                            return offscreenPremultipliedSolidPipeline;
                                        case BlendMode::Multiply:
                                            return offscreenMultiplySolidPipeline;
                                        case BlendMode::Alpha:
                                            return offscreenSolidAlphaPipeline;
                                        case BlendMode::Opaque:
                                        default:
                                            return offscreenSolidPipeline;
                                    }
                                };

                                auto drawOffscreenTextured = [&](TexturedBatch &tb) {
                                    if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) return;
                                    auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
                                    vk::DescriptorSet texSet = gpu->descriptorSet;
                                    if ((tb.depth && tb.depth->gpuHandle) ||
                                        (tb.motion && tb.motion->gpuHandle) ||
                                        (tb.extra && tb.extra->gpuHandle)) {
                                        auto *depthGpu = tb.depth
                                                             ? static_cast<GpuTexture *>(tb.depth->gpuHandle)
                                                             : nullptr;
                                        auto *motionGpu = tb.motion
                                                              ? static_cast<GpuTexture *>(tb.motion->gpuHandle)
                                                              : nullptr;
                                        auto *extraGpu = tb.extra
                                                             ? static_cast<GpuTexture *>(tb.extra->gpuHandle)
                                                             : nullptr;
                                        auto *specularGpu =
                                            tb.specular
                                                ? static_cast<GpuTexture *>(tb.specular->gpuHandle)
                                                : nullptr;
                                        if (vk::DescriptorSet combo =
                                                post2SetFor(gpu, depthGpu, motionGpu, extraGpu,
                                                            specularGpu))
                                            texSet = combo;
                                    }
                                    Batcher ndc = tb.batch;
                                    ndc.toNDC(vw, vh);
                                    std::vector<TexturedVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

                                    if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
                                    vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
                                    vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

                                    if (tb.shader && tb.shader->gpuHandle) {
                                        if (canvas->isHDR())
                                            ensureShaderHdrOffscreenPipeline(tb.shader);
                                        else
                                            ensureShaderOffscreenPipeline(tb.shader);
                                        auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
                                        vk::Pipeline alphaPipe = canvas->isHDR()
                                                                     ? gs->hdrOffscreenPipeline
                                                                     : gs->offscreenPipeline;
                                        vk::Pipeline opaquePipe =
                                            canvas->isHDR() ? gs->hdrOffscreenOpaquePipeline
                                                            : gs->offscreenOpaquePipeline;
                                        if (!alphaPipe) return;
                                        vk::Pipeline customPipeline =
                                            tb.blend == BlendMode::Opaque && opaquePipe
                                                ? opaquePipe
                                                : alphaPipe;
                                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                        customPipeline);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                              shaderPipelineLayout, 0, 1,
                                                              &texSet, 0, nullptr);
                                        cb.pushConstants(shaderPipelineLayout,
                                                         vk::ShaderStageFlagBits::eVertex |
                                                             vk::ShaderStageFlagBits::eFragment,
                                                         0, Shader::kPushConstantBytes,
                                                         tb.shader->pushConstantData());
                                    } else {
                                        vk::Pipeline pipe = offscreenTexPipe(tb.blend);
                                        if (!pipe) return;
                                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                              texPipelineLayout, 0, 1,
                                                              &texSet, 0, nullptr);
                                    }
                                    vk::DeviceSize offset = 0;
                                    cb.bindVertexBuffers(0, 1, vb, &offset);
                                    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                };

                                std::vector<bool> solidUploaded(solid.size(), false);
                                auto uploadSolid = [&](size_t idx) {
                                    if (idx >= solid.size() || solidUploaded[idx] ||
                                        solid[idx].batch.empty())
                                        return;
                                    vk::Pipeline pipe = offscreenSolidPipe(solid[idx].blend);
                                    if (!pipe) return;
                                    Batcher ndc = solid[idx].batch;
                                    ndc.toNDC(vw, vh);
                                    std::vector<ColorVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(ColorVertex{v.pos, v.color});
                                    if (solidBufs.size() <= idx) solidBufs.resize(idx + 1);
                                    solidBufs[idx].allocate<ColorVertex>(frameToken(), device,
                                                                         gpuVerts);
                                    solidUploaded[idx] = true;
                                };

                                auto drawSolidSpan = [&](uint32_t batchIndex, uint32_t begin,
                                                         uint32_t count) {
                                    if (batchIndex >= solid.size() || count == 0 ||
                                        solid[batchIndex].batch.empty())
                                        return;
                                    vk::Pipeline pipe = offscreenSolidPipe(solid[batchIndex].blend);
                                    if (!pipe) return;
                                    uploadSolid(batchIndex);
                                    vk::DeviceSize offset = 0;
                                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
                                    cb.bindVertexBuffers(0, 1, solidBufs[batchIndex], &offset);
                                    cb.draw(count, 1, begin, 0);
                                };

                                if (!spans.empty()) {
                                    for (const auto &sp : spans) {
                                        if (sp.kind == OverlayKind::Solid)
                                            drawSolidSpan(sp.index, sp.vertBegin, sp.vertCount);
                                        else if (sp.kind == OverlayKind::Textured &&
                                                 sp.index < textured.size())
                                            drawOffscreenTextured(textured[sp.index]);
                                        else if (sp.kind == OverlayKind::Lit && offscreenLitPipeline &&
                                                 sp.index < lit.size()) {
                                            std::vector<LitBatch> one;
                                            one.push_back(std::move(lit[sp.index]));
                                            drawLitBatches(cb, vw, vh, offscreenLitPipeline, one,
                                                           texBufs, texBufIndex, true);
                                        }
                                    }
                                } else {
                                    for (size_t i = 0; i < solid.size(); ++i) {
                                        if (!solid[i].batch.empty())
                                            drawSolidSpan(uint32_t(i), 0,
                                                          uint32_t(solid[i].batch.vertices().size()));
                                    }
                                    for (auto &tb : textured) drawOffscreenTextured(tb);
                                    if (offscreenLitPipeline)
                                        drawLitBatches(cb, vw, vh, offscreenLitPipeline, lit, texBufs,
                                                       texBufIndex, true);
                                }

                                cb.endRenderPass();
                                canvas->colorImage().endSampledLayout();
                            };
    if (swapchainPassOpen && !sceneColorPassOpen && presentRecording) {
        recordOffscreen(currentPresentCb());
    } else {
        vkb::executeImmediately(device.instance, uploadPool,
                                device.getQueue(vkb::QueueType::graphics), recordOffscreen);
    }
}


} // namespace eve::graphics::vulkan
