#include "common/FrameCapture.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/CubemapPrefilter.h"
#include "graphics/GBuffer.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/ReflectionProbeUpload.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/GraphicsPipelineSupport.h"
#include "graphics/webgpu/InitFlow.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include "common/Exception.h"
#include "common/Resource.h"
#include "common/config.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace eve::graphics::webgpu {
using namespace detail;

uint32_t Graphics::UboArena::alloc(uint64_t size, uint64_t alignment) {
    uint64_t aligned = (used + alignment - 1) / alignment * alignment;
    used             = aligned + size;
    return static_cast<uint32_t>(aligned);
}

uint64_t Graphics::VertexArena::alloc(uint64_t bytes) {
    uint64_t at = used;
    used += bytes;
    return at;
}

Graphics::UboArena&    Graphics::currentUboArena() { return uboArenas[currentFrameSlot()]; }
Graphics::VertexArena& Graphics::currentVertexArena() { return vertexArenas[currentFrameSlot()]; }

void Graphics::ensureUboArena(UboArena& arena, uint64_t bytes) {
    if (arena.buffer && arena.capacity >= bytes) return;
    uint64_t size = arena.capacity;
    while (size < bytes) size = size ? size * 2 : (64u << 10);
    WGPUBufferDescriptor bd{};
    bd.label            = sv("eve_ubo_arena");
    bd.size             = size;
    bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    bd.mappedAtCreation = false;
    arena.buffer        = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    arena.capacity      = size;
    arena.used          = 0;
}

void Graphics::ensureVertexArena(VertexArena& arena, uint64_t bytes) {
    if (arena.buffer && arena.capacity >= bytes) return;
    uint64_t size = arena.capacity;
    while (size < bytes) size = size ? size * 2 : (256u << 10);
    WGPUBufferDescriptor bd{};
    bd.label            = sv("eve_vertex_arena");
    bd.size             = size;
    bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bd.mappedAtCreation = false;
    arena.buffer        = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    arena.capacity      = size;
    arena.used          = 0;
}


// ---------------------------------------------------------------------------
// 2D drawing
// ---------------------------------------------------------------------------

void Graphics::clear2DBatches() {
    solidBatches.clear();
    texturedBatches.clear();
    litBatches.clear();
    overlaySpans.clear();
    sceneColorComposited = false;
}

void Graphics::noteSolidOverlay(uint32_t idx) {
    if (idx >= solidBatches.size()) return;
    const uint32_t n = uint32_t(solidBatches[idx].batch.vertices().size());
    if (!overlaySpans.empty() && overlaySpans.back().kind == OverlayKind::Solid && overlaySpans.back().index == idx) {
        overlaySpans.back().vertCount = n - overlaySpans.back().vertBegin;
        return;
    }
    const uint32_t begin = n >= 6u ? n - 6u : 0u;
    overlaySpans.push_back({OverlayKind::Solid, idx, begin, n - begin});
}

void Graphics::noteTexturedOverlay(Texture* tex, uint32_t idx) {
    if (tex && tex == getSceneColorTexture()) sceneColorComposited = true;
    if (!overlaySpans.empty() && overlaySpans.back().kind == OverlayKind::Textured && overlaySpans.back().index == idx)
        return;
    overlaySpans.push_back({OverlayKind::Textured, idx, 0, 0});
}

void Graphics::noteLitOverlay(uint32_t idx) {
    if (!overlaySpans.empty() && overlaySpans.back().kind == OverlayKind::Lit && overlaySpans.back().index == idx)
        return;
    overlaySpans.push_back({OverlayKind::Lit, idx, 0, 0});
}

void Graphics::drawSolidRect(float x, float y, float w, float h, const Color& color, BlendMode blend) {
    auto it =
        std::find_if(solidBatches.begin(), solidBatches.end(), [&](const SolidBatch& sb) { return sb.blend == blend; });
    if (it == solidBatches.end()) {
        solidBatches.push_back(SolidBatch{blend, Batcher{}});
        it = solidBatches.end() - 1;
    }
    it->batch.addRect(x, y, w, h, color);
    noteSolidOverlay(uint32_t(it - solidBatches.begin()));
}

void Graphics::drawPrimitiveCanvas(const PrimitiveCanvas2D& canvas) {
    const int  targetWidth  = activeCanvas ? activeCanvas->getWidth() : getWidth();
    const int  targetHeight = activeCanvas ? activeCanvas->getHeight() : getHeight();
    const auto triangles    = resolvePrimitiveStrokes2D(canvas, {targetWidth, targetHeight});
    if (triangles.vertices.empty()) return;
    auto logicalPoint = [targetWidth, targetHeight](const PrimitiveTriangleVertex& vertex) {
        const glm::vec2 ndc = glm::vec2(vertex.clipPosition) / vertex.clipPosition.w;
        return glm::vec2((ndc.x + 1.f) * 0.5f * static_cast<float>(targetWidth),
                         (ndc.y + 1.f) * 0.5f * static_cast<float>(targetHeight));
    };
    for (const ResolvedPrimitiveBatch2D& resolvedBatch : triangles.batches2D) {
        auto it = std::find_if(solidBatches.begin(), solidBatches.end(),
                               [&](const SolidBatch& batch) { return batch.blend == resolvedBatch.blend; });
        if (it == solidBatches.end()) {
            solidBatches.push_back(SolidBatch{resolvedBatch.blend, Batcher{}});
            it = solidBatches.end() - 1;
        }
        for (std::size_t i = resolvedBatch.firstVertex; i < resolvedBatch.firstVertex + resolvedBatch.vertexCount;
             i += 3) {
            it->batch.addTriangle(logicalPoint(triangles.vertices[i]), logicalPoint(triangles.vertices[i + 1]),
                                  logicalPoint(triangles.vertices[i + 2]), triangles.vertices[i].color,
                                  triangles.vertices[i + 1].color, triangles.vertices[i + 2].color);
        }
        const auto          batchIndex = static_cast<std::uint32_t>(it - solidBatches.begin());
        const std::uint32_t count      = static_cast<std::uint32_t>(resolvedBatch.vertexCount);
        const std::uint32_t end        = static_cast<std::uint32_t>(it->batch.vertices().size());
        overlaySpans.push_back({OverlayKind::Solid, batchIndex, end - count, count});
    }
}

void Graphics::drawSolidRectRotated(float cx, float cy, float w, float h, float degrees, const Color& color,
                                    BlendMode blend) {
    auto it =
        std::find_if(solidBatches.begin(), solidBatches.end(), [&](const SolidBatch& sb) { return sb.blend == blend; });
    if (it == solidBatches.end()) {
        solidBatches.push_back(SolidBatch{blend, Batcher{}});
        it = solidBatches.end() - 1;
    }
    it->batch.addRectRotated(cx, cy, w, h, degrees, color);
    noteSolidOverlay(uint32_t(it - solidBatches.begin()));
}

void Graphics::drawTexturedRect(Texture* texture, float x, float y, float w, float h, const Color& color) {
    drawTexturedRectUV(texture, x, y, w, h, 0.f, 0.f, 1.f, 1.f, color);
}

void Graphics::drawTexturedRectShader(Texture* texture, Shader* shader, float x, float y, float w, float h,
                                      const Color& color) {
    drawTexturedRectShaderUV(texture, shader, x, y, w, h, 0.f, 0.f, 1.f, 1.f, color);
}

void Graphics::drawTexturedRectUV(Texture* texture, float x, float y, float w, float h, float u0, float v0, float u1,
                                  float v1, const Color& color) {
    drawTexturedRectShaderUV(texture, currentShader, x, y, w, h, u0, v0, u1, v1, color);
}

void Graphics::drawTexturedRectShaderUV(Texture* texture, Shader* shader, float x, float y, float w, float h, float u0,
                                        float v0, float u1, float v1, const Color& color, bool rotatedUV,
                                        BlendMode blend) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color, blend);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().shader != shader || texturedBatches.back().depth != nullptr ||
        texturedBatches.back().blend != blend) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, blend, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1, rotatedUV);
    noteTexturedOverlay(texture, uint32_t(texturedBatches.size() - 1));
}

void Graphics::drawTexturedRectShaderUVRotated(Texture* texture, Shader* shader, float cx, float cy, float w, float h,
                                               float degrees, float u0, float v0, float u1, float v1,
                                               const Color& color, bool rotatedUV, BlendMode blend) {
    if (!texture) {
        drawSolidRect(cx - w * 0.5f, cy - h * 0.5f, w, h, color, blend);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().shader != shader || texturedBatches.back().depth != nullptr ||
        texturedBatches.back().blend != blend) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, blend, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRectRotated(cx, cy, w, h, degrees, color, u0, v0, u1, v1, rotatedUV);
    noteTexturedOverlay(texture, uint32_t(texturedBatches.size() - 1));
}

void Graphics::drawTexturedRectShaderDepth(Texture* color, Texture* depth, Shader* shader, float x, float y, float w,
                                           float h, const Color& tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != color || texturedBatches.back().shader != shader ||
        texturedBatches.back().depth != depth || texturedBatches.back().blend != BlendMode::Alpha) {
        texturedBatches.push_back(TexturedBatch{color, depth, shader, BlendMode::Alpha, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f, false);
    noteTexturedOverlay(color, uint32_t(texturedBatches.size() - 1));
}

void Graphics::drawTexturedRectShaderDepthMotion(Texture* color, Texture* depth, Texture* motion, Shader* shader,
                                                 float x, float y, float w, float h, const Color& tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != color || texturedBatches.back().shader != shader ||
        texturedBatches.back().depth != depth || texturedBatches.back().motion != motion ||
        texturedBatches.back().blend != BlendMode::Opaque) {
        TexturedBatch batch{color, depth, shader, BlendMode::Opaque, Batcher{}};
        batch.motion = motion;
        texturedBatches.push_back(std::move(batch));
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f, false);
    noteTexturedOverlay(color, uint32_t(texturedBatches.size() - 1));
}

void Graphics::drawTexturedRectShader4(Texture* color, Texture* depth, Texture* motion, Texture* extra, Shader* shader,
                                       float x, float y, float w, float h, const Color& tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != color || texturedBatches.back().shader != shader ||
        texturedBatches.back().depth != depth || texturedBatches.back().motion != motion ||
        texturedBatches.back().extra != extra || texturedBatches.back().blend != BlendMode::Opaque) {
        TexturedBatch batch{color, depth, shader, BlendMode::Opaque, Batcher{}};
        batch.motion = motion;
        batch.extra  = extra;
        texturedBatches.push_back(std::move(batch));
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f, false);
    noteTexturedOverlay(color, uint32_t(texturedBatches.size() - 1));
}

void Graphics::drawTexturedRectShader5(Texture* color, Texture* depth, Texture* motion, Texture* extra,
                                       Texture* specular, Shader* shader, float x, float y, float w, float h,
                                       const Color& tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != color || texturedBatches.back().shader != shader ||
        texturedBatches.back().depth != depth || texturedBatches.back().motion != motion ||
        texturedBatches.back().extra != extra || texturedBatches.back().specular != specular ||
        texturedBatches.back().blend != BlendMode::Opaque) {
        TexturedBatch batch{color, depth, shader, BlendMode::Opaque, Batcher{}};
        batch.motion   = motion;
        batch.extra    = extra;
        batch.specular = specular;
        texturedBatches.push_back(std::move(batch));
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f, false);
    noteTexturedOverlay(color, uint32_t(texturedBatches.size() - 1));
}

void Graphics::drawTexturedRectLitUV(Texture* albedo, Texture* normal, float x, float y, float w, float h, float u0,
                                     float v0, float u1, float v1, const Color& color, BlendMode blend) {
    if (!albedo) {
        drawSolidRect(x, y, w, h, color, blend);
        return;
    }
    if (litBatches.empty() || litBatches.back().albedo != albedo || litBatches.back().normal != normal ||
        litBatches.back().blend != blend) {
        litBatches.push_back(LitBatch{albedo, normal, blend, Batcher{}});
    }
    litBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1, false);
    noteLitOverlay(uint32_t(litBatches.size() - 1));
}

void Graphics::drawTexturedRectLitUVRotated(Texture* albedo, Texture* normal, float cx, float cy, float w, float h,
                                            float degrees, float u0, float v0, float u1, float v1, const Color& color,
                                            BlendMode blend) {
    if (!albedo) {
        drawSolidRectRotated(cx, cy, w, h, degrees, color, blend);
        return;
    }
    if (litBatches.empty() || litBatches.back().albedo != albedo || litBatches.back().normal != normal ||
        litBatches.back().blend != blend) {
        litBatches.push_back(LitBatch{albedo, normal, blend, Batcher{}});
    }
    litBatches.back().batch.addTexturedRectRotated(cx, cy, w, h, degrees, color, u0, v0, u1, v1, false);
    noteLitOverlay(uint32_t(litBatches.size() - 1));
}

void Graphics::setLighting2D(const Lighting2DUBO& ubo) {
    // Uploaded on demand before flushing lit batches.
    lighting2dFrame = ubo;
}

void Graphics::flush2D(wgpu::RenderPassEncoder pass, int viewW, int viewH, WGPUTextureFormat format) {
    ensureFileTexturesReady();
    applyPendingResourceChanges();
    auto       spans     = std::move(overlaySpans);
    const bool offscreen = uint32_t(format) != uint32_t(surfaceFormat);

    struct SolidUpload {
        BlendMode blend  = BlendMode::Alpha;
        uint64_t  offset = 0;
        uint64_t  bytes  = 0;
    };
    std::vector<SolidUpload> solidUploads;
    solidUploads.reserve(solidBatches.size());
    for (const auto& sb : solidBatches) {
        if (sb.batch.empty()) {
            solidUploads.push_back(SolidUpload{sb.blend, 0, 0});
            continue;
        }
        Batcher ndc = sb.batch;
        ndc.toNDC(viewW, viewH);
        auto verts = ndc.vertices();
        // Batcher::toNDC targets Vulkan's Y-down NDC ((-1,-1)=top-left), but
        // WebGPU's clip space is Y-up ((-1,-1)=bottom-left). Flip Y so logical
        // top-left maps to the swapchain's top-left.
        for (auto& v : verts) v.pos.y = -v.pos.y;
        std::vector<float> data;
        data.reserve(verts.size() * 6);
        for (const auto& v : verts) {
            data.push_back(v.pos.x);
            data.push_back(v.pos.y);
            data.push_back(v.color.r);
            data.push_back(v.color.g);
            data.push_back(v.color.b);
            data.push_back(v.color.a);
        }
        const uint64_t bytes = data.size() * sizeof(float);
        auto&          arena = currentVertexArena();
        ensureVertexArena(arena, arena.used + bytes);
        const uint64_t offset = arena.alloc(bytes);
        queue.WriteBuffer(arena.buffer, offset, data.data(), bytes);
        solidUploads.push_back(SolidUpload{sb.blend, offset, bytes});
    }

    auto solidPipe = [&](BlendMode mode) -> wgpu::RenderPipeline { return get2DColorPipeline(mode, offscreen); };

    auto drawSolid = [&](uint32_t batchIndex, uint32_t first, uint32_t count) {
        if (batchIndex >= solidUploads.size()) return;
        const SolidUpload& u = solidUploads[batchIndex];
        if (u.bytes == 0 || count == 0) return;
        pass.SetPipeline(solidPipe(u.blend));
        pass.SetVertexBuffer(0, currentVertexArena().buffer, u.offset, u.bytes);
        pass.Draw(count, 1, first, 0);
    };

    if (!spans.empty()) {
        for (const auto& sp : spans) {
            if (sp.kind == OverlayKind::Solid)
                drawSolid(sp.index, sp.vertBegin, sp.vertCount);
            else if (sp.kind == OverlayKind::Textured && sp.index < texturedBatches.size())
                drawTexturedBatch(pass, texturedBatches[sp.index], viewW, viewH, format, offscreen);
            else if (sp.kind == OverlayKind::Lit && sp.index < litBatches.size())
                drawLitBatch(pass, litBatches[sp.index], viewW, viewH, format);
        }
    } else {
        for (size_t i = 0; i < solidUploads.size(); ++i) {
            if (solidUploads[i].bytes > 0) drawSolid(uint32_t(i), 0, uint32_t(solidBatches[i].batch.vertices().size()));
        }
        for (auto& tb : texturedBatches) {
            if (tb.batch.empty()) continue;
            drawTexturedBatch(pass, tb, viewW, viewH, format, offscreen);
        }
        for (auto& lb : litBatches) {
            if (lb.batch.empty()) continue;
            drawLitBatch(pass, lb, viewW, viewH, format);
        }
    }
    clear2DBatches();
}

void Graphics::drawTexturedBatch(wgpu::RenderPassEncoder pass, TexturedBatch& tb, int viewW, int viewH,
                                 WGPUTextureFormat format, bool offscreen) {
    Batcher ndc = tb.batch;
    ndc.toNDC(viewW, viewH);
    auto verts = ndc.vertices();
    for (auto& v : verts) v.pos.y = -v.pos.y;
    if (verts.empty()) return;

    std::vector<float> data;
    data.reserve(verts.size() * 8);
    for (const auto& v : verts) {
        data.push_back(v.pos.x);
        data.push_back(v.pos.y);
        data.push_back(v.color.r);
        data.push_back(v.color.g);
        data.push_back(v.color.b);
        data.push_back(v.color.a);
        data.push_back(v.uv.x);
        data.push_back(v.uv.y);
    }
    uint64_t bytes = data.size() * sizeof(float);
    auto&    arena = currentVertexArena();
    ensureVertexArena(arena, arena.used + bytes);
    uint64_t vtxOffset = arena.alloc(bytes);
    queue.WriteBuffer(arena.buffer, vtxOffset, data.data(), bytes);

    GpuTexture* gpu         = gpuForTexture(tb.texture);
    GpuTexture* depthGpu    = gpuForTexture(tb.depth);
    GpuTexture* motionGpu   = gpuForTexture(tb.motion);
    GpuTexture* extraGpu    = gpuForTexture(tb.extra);
    GpuTexture* specularGpu = gpuForTexture(tb.specular);
    auto&       uboArena    = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + 256);

    // Push-constant (Externals) block for custom shaders.
    uint32_t pushOffset = 0;
    if (tb.shader && tb.shader->pushConstantSize() > 0) {
        pushOffset = uboArena.alloc(Shader::kPushConstantBytes, 256);
        queue.WriteBuffer(uboArena.buffer, pushOffset, tb.shader->pushConstantData(), Shader::kPushConstantBytes);
    }

    wgpu::BindGroup bg         = makeTex2DBindGroup(gpu, depthGpu, motionGpu, extraGpu, specularGpu);
    uint32_t        offsets[1] = {pushOffset};

    wgpu::RenderPipeline pipe;
    if (tb.shader && tb.shader->gpuHandle) {
        auto*      gs  = static_cast<GpuShader*>(tb.shader->gpuHandle);
        const bool hdr = format == WGPUTextureFormat_RGBA16Float;
        if (tb.blend == BlendMode::Opaque)
            pipe = hdr ? gs->hdrOffscreenOpaquePipeline
                       : (offscreen ? gs->offscreenOpaquePipeline : gs->swapchainOpaquePipeline);
        else
            pipe = hdr ? gs->hdrOffscreenPipeline : (offscreen ? gs->offscreenPipeline : gs->swapchainPipeline);
    } else {
        switch (tb.blend) {
            case BlendMode::Additive:
                pipe = offscreen ? offscreenTexturedAdditivePipeline : texturedAdditivePipeline;
                break;
            case BlendMode::Premultiplied:
                pipe = offscreen ? offscreenTexturedPremultipliedPipeline : texturedPremultipliedPipeline;
                break;
            case BlendMode::Multiply:
                pipe = offscreen ? offscreenTexturedMultiplyPipeline : texturedMultiplyPipeline;
                break;
            case BlendMode::Opaque:
                pipe = format == WGPUTextureFormat_RGBA16Float
                           ? hdrOffscreenTexturedOpaquePipeline
                           : (offscreen ? offscreenTexturedOpaquePipeline : texturedOpaquePipeline);
                break;
            case BlendMode::Alpha:
            default:
                pipe = format == WGPUTextureFormat_RGBA16Float
                           ? hdrOffscreenTexturedPipeline
                           : (offscreen ? offscreenTexturedPipeline : texturedPipeline);
                break;
        }
    }
    if (!pipe) return;
    pass.SetPipeline(pipe);
    pass.SetBindGroup(0, bg, 1, offsets);
    pass.SetVertexBuffer(0, arena.buffer, vtxOffset, bytes);
    pass.Draw(static_cast<uint32_t>(verts.size()), 1, 0, 0);
}

void Graphics::drawLitBatch(wgpu::RenderPassEncoder pass, LitBatch& lb, int viewW, int viewH,
                            WGPUTextureFormat format) {
    Batcher ndc = lb.batch;
    ndc.toNDC(viewW, viewH);
    auto verts = ndc.vertices();
    for (auto& v : verts) v.pos.y = -v.pos.y;
    if (verts.empty()) return;

    std::vector<float> data;
    data.reserve(verts.size() * 8);
    for (const auto& v : verts) {
        data.push_back(v.pos.x);
        data.push_back(v.pos.y);
        data.push_back(v.color.r);
        data.push_back(v.color.g);
        data.push_back(v.color.b);
        data.push_back(v.color.a);
        data.push_back(v.uv.x);
        data.push_back(v.uv.y);
    }
    uint64_t bytes = data.size() * sizeof(float);
    auto&    arena = currentVertexArena();
    ensureVertexArena(arena, arena.used + bytes);
    uint64_t vtxOffset = arena.alloc(bytes);
    queue.WriteBuffer(arena.buffer, vtxOffset, data.data(), bytes);

    GpuTexture* albedoGpu = gpuForTextureOrWhite(lb.albedo);
    // Null / missing normal → flat +Z (128,128,255), not white (which is a
    // bright wrong normal and washes out lit sprites without a map).
    GpuTexture* normalGpu = gpuForTexture(lb.normal);
    if (!normalGpu) normalGpu = flatNormalTexture ? flatNormalTexture : whiteTexture;

    auto& uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + 512);
    uint32_t uboOffset = uboArena.alloc(sizeof(Lighting2DUBO), 256);
    queue.WriteBuffer(uboArena.buffer, uboOffset, &lighting2dFrame, sizeof(Lighting2DUBO));

    wgpu::BindGroup      bg         = makeTex2DBindGroup(albedoGpu, normalGpu);
    uint32_t             offsets[1] = {uboOffset};
    wgpu::RenderPipeline pipe       = get2DLitPipeline(lb.blend, format);
    if (!pipe) return;
    pass.SetPipeline(pipe);
    pass.SetBindGroup(0, bg, 1, offsets);
    pass.SetVertexBuffer(0, arena.buffer, vtxOffset, bytes);
    pass.Draw(static_cast<uint32_t>(verts.size()), 1, 0, 0);
}

// ---------------------------------------------------------------------------
// 3D rendering
// ---------------------------------------------------------------------------

void Graphics::begin3DFrame() {
    if (!initialized || !device) return;
    ensureFileTexturesReady();
    applyPendingResourceChanges();
    frame3DStarted      = true;
    frameHad3DThisFrame = true;
    // Match the desktop (Vulkan) backend: had3DThisFrame() must return true
    // once a 3D frame begins, or RenderSystem3D::render bails out before any
    // mesh is drawn.
    frameHad3D         = true;
    sceneColorPassOpen = false;
    mesh3dDraws.clear();
    primitive3DDraws.clear();
    shadowPassDraws.clear();
    voxelDraws.clear();
    if (surfaceNeedsRecreate.load()) {
        surfaceNeedsRecreate.store(false);
        markSwapchainDirty();
    }
    rebuildSwapchainIfNeeded();
}


void Graphics::begin3DFrameToCanvas(Canvas* canvas) {
    if (!canvas) throw Exception("begin3DFrameToCanvas: null canvas");
    if (!device) throw Exception("begin3DFrameToCanvas: device not initialized");
    auto* offscreen = dynamic_cast<OffscreenCanvas*>(canvas);
    if (!offscreen) throw Exception("begin3DFrameToCanvas: not an offscreen canvas");
    clearColor     = backgroundColor;
    active3DCanvas = offscreen;
    begin3DFrame();
}
void Graphics::end3DFrameToCanvas() {
    OffscreenCanvas* canvas = active3DCanvas;
    if (!canvas || !device) return;
    auto& uboArena = currentUboArena();
    uboArena.reset();
    ensureUboArena(uboArena, 4096);
    auto& vertexArena = currentVertexArena();
    vertexArena.reset();

    wgpuInstanceProcessEvents(instance.Get());
    OffscreenTimestampReadback* timestampSlot = nullptr;
    if (offscreenTimestampSupported && offscreenTimestampQuerySet && offscreenTimestampResolveBuffer) {
        for (size_t candidate = 0; candidate < offscreenTimestampReadbacks.size(); ++candidate) {
            const size_t index = (offscreenTimestampReadbackCursor + candidate) % offscreenTimestampReadbacks.size();
            if (!offscreenTimestampReadbacks[index].pending) {
                timestampSlot                    = &offscreenTimestampReadbacks[index];
                offscreenTimestampReadbackCursor = (index + 1) % offscreenTimestampReadbacks.size();
                break;
            }
        }
    }

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    recordDirectionalShadowPasses(encoder);
    WGPURenderPassColorAttachment color{};
    color.view              = canvas->colorView.Get();
    color.depthSlice        = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp            = WGPULoadOp_Clear;
    color.storeOp           = WGPUStoreOp_Store;
    const Color canvasClear = canvas->clearRequested ? canvas->clearColor : clearColor;
    color.clearValue        = {canvasClear.r, canvasClear.g, canvasClear.b, canvasClear.a};
    WGPURenderPassDepthStencilAttachment depth{};
    depth.view            = canvas->depthView.Get();
    depth.depthClearValue = 1.f;
    depth.depthLoadOp     = WGPULoadOp_Clear;
    depth.depthStoreOp    = WGPUStoreOp_Store;
    depth.stencilLoadOp   = WGPULoadOp_Undefined;
    depth.stencilStoreOp  = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount   = 1;
    descriptor.colorAttachments       = &color;
    descriptor.depthStencilAttachment = &depth;
    WGPUPassTimestampWrites timestampWrites{};
    if (timestampSlot) {
        timestampWrites.querySet                  = offscreenTimestampQuerySet.Get();
        timestampWrites.beginningOfPassWriteIndex = 0;
        timestampWrites.endOfPassWriteIndex       = 1;
        descriptor.timestampWrites                = &timestampWrites;
    }
    wgpu::RenderPassEncoder pass =
        encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&descriptor));
    flushMesh3D(pass, canvas->isHDR() ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8Unorm, true);
    flushPrimitive3D(pass, canvas->isHDR() ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8Unorm, 1);
    pass.End();
    if (timestampSlot) {
        encoder.ResolveQuerySet(offscreenTimestampQuerySet, 0, 2, offscreenTimestampResolveBuffer, 0);
        encoder.CopyBufferToBuffer(offscreenTimestampResolveBuffer, 0, timestampSlot->buffer, 0, sizeof(uint64_t) * 2);
    }
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
    if (timestampSlot) {
        timestampSlot->pending = true;
        WGPUBufferMapCallbackInfo callback{};
        callback.mode     = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
            auto* slot = static_cast<OffscreenTimestampReadback*>(userdata1);
            if (status == WGPUMapAsyncStatus_Success && slot && slot->owner) {
                const auto* ticks =
                    static_cast<const uint64_t*>(slot->buffer.GetConstMappedRange(0, sizeof(uint64_t) * 2));
                if (ticks && ticks[1] >= ticks[0]) {
                    const float milliseconds = float(ticks[1] - ticks[0]) * 1.0e-6f;
                    slot->owner->completedOffscreenTimestampMs.fetch_add(milliseconds);
                }
                slot->buffer.Unmap();
            }
            if (slot) slot->pending = false;
        };
        callback.userdata1 = timestampSlot;
        wgpuBufferMapAsync(timestampSlot->buffer.Get(), WGPUMapMode_Read, 0, sizeof(uint64_t) * 2, callback);
    }
    canvas->clearRequested = false;
    active3DCanvas         = nullptr;
    frame3DStarted         = false;
    frameHad3DThisFrame    = false;
    frameHad3D             = false;
}

void Graphics::setMesh3DViewProj(const glm::mat4& viewProj) { mesh3dViewProj = viewProj; }
void Graphics::setMesh3DView(const glm::mat4& view) { mesh3dView = view; }
void Graphics::setMesh3DClip(float nearZ, float farZ) {
    mesh3dNear = nearZ;
    mesh3dFar  = farZ;
}

Texture* Graphics::getSceneColorTexture() { return sceneColorTexture; }

Texture* Graphics::getSceneLinearDepthTexture() {
    GBuffer* gbuffer = renderControl_ ? renderControl_->getGBuffer() : nullptr;
    return gbuffer && gbuffer->isValid() ? gbuffer->getDepthTexture() : nullptr;
}

void Graphics::drawMesh(Mesh* mesh, const glm::mat4& model, Texture* texture, const Color& tint) {
    drawMeshShader(mesh, model, texture, tint, nullptr);
}

void Graphics::drawMeshShader(Mesh* mesh, const glm::mat4& model, Texture* texture, const Color& tint, Shader* shader) {
    if (!mesh || !mesh->gpuHandle) return;
    frameHad3DThisFrame = true;
    frameHad3D          = true;
    Mesh3dDraw d;
    d.mesh                 = mesh;
    d.texture              = texture;
    d.normalTexture        = mesh3dNormalTexture;
    d.heightTexture        = mesh3dHeightTexture;
    d.virtualTexture       = mesh3dVirtualTexture;
    d.virtualAtlas         = mesh3dVirtualAtlas;
    d.model                = model;
    d.tint                 = tint;
    d.shader               = shader;
    d.surfaceMode          = mesh3dSurfaceMode;
    d.surfaceBlend         = mesh3dSurfaceBlend;
    d.depthWrite           = mesh3dSurfaceDepthWrite;
    d.doubleSided          = mesh3dSurfaceDoubleSided;
    d.shadowReceive        = mesh3dShadowReceive;
    d.metallic             = mesh3dMetallic;
    d.roughness            = mesh3dRoughness;
    d.viewProj             = mesh3dViewProj;
    d.view                 = mesh3dView;
    d.cameraPos            = mesh3dCameraPos;
    d.lighting             = mesh3dLighting;
    d.shadows              = mesh3dShadows;
    d.environment          = mesh3dEnvTexture;
    d.environmentIntensity = mesh3dEnvIntensity;
    d.skinInfluenceLimit   = mesh3dSkinInfluenceLimit;
    d.alphaCutoff          = mesh3dAlphaCutoff;
    d.alphaTechnique       = mesh3dAlphaTechnique;
    d.lodFade              = mesh3dLodFade;
    d.pbrSurface           = mesh3dPbrSurface;
    mesh3dDraws.push_back(d);
}

void Graphics::setMesh3DNormalTexture(Texture* normal) { mesh3dNormalTexture = normal; }
void Graphics::setMesh3DPackedNormalMask(bool enabled) { mesh3dVirtualAtlas.z = enabled ? 1.f : 0.f; }
void Graphics::setMesh3DHeightTexture(Texture* height) { mesh3dHeightTexture = height; }

void Graphics::setMesh3DVirtualTexture(bool enabled, int pageCountX, int pageCountY, int atlasSlotsX, int atlasSlotsY,
                                       float borderFraction) {
    mesh3dVirtualTexture =
        enabled ? glm::vec4(1.f, float(pageCountX), float(pageCountY), borderFraction) : glm::vec4(0.f);
    const float packedNormalMask = mesh3dVirtualAtlas.z;
    mesh3dVirtualAtlas           = enabled ? glm::vec4(float(atlasSlotsX), float(atlasSlotsY), packedNormalMask, 0.f)
                                           : glm::vec4(0.f, 0.f, packedNormalMask, 0.f);
}
void Graphics::setMesh3DSceneDepth(Texture* depth) { mesh3dSceneDepthTexture = depth; }
void Graphics::setMesh3DSceneColor(Texture* color) { mesh3dSceneColorTexture = color; }

Graphics::Mesh3DSceneColorCaptureStatus Graphics::captureMesh3DSceneColor() {
    if (mesh3dSceneColorTexture) return Mesh3DSceneColorCaptureStatus::ExplicitOverride;
    if (active3DCanvas)
        return sceneColorHistoryValid ? Mesh3DSceneColorCaptureStatus::HistoryReuse
                                      : Mesh3DSceneColorCaptureStatus::Unavailable;
    if (sceneColorSlots.size() < 2 || sceneColorSamples != 1)
        return sceneColorHistoryValid ? Mesh3DSceneColorCaptureStatus::HistoryReuse
                                      : Mesh3DSceneColorCaptureStatus::Unavailable;
    if (!mesh3dSceneColorCaptureIndex) mesh3dSceneColorCaptureIndex = mesh3dDraws.size();
    return Mesh3DSceneColorCaptureStatus::Scheduled;
}
void Graphics::setMesh3DMaterial(float metallic, float roughness) {
    mesh3dMetallic  = metallic;
    mesh3dRoughness = roughness;
}
void Graphics::setMesh3DSurface(SurfaceMode mode, BlendMode blend, bool depthWrite, bool doubleSided, float alphaCutoff,
                                const std::string& alphaTechnique) {
    mesh3dSurfaceMode        = mode;
    mesh3dSurfaceBlend       = blend;
    mesh3dSurfaceDepthWrite  = depthWrite;
    mesh3dSurfaceDoubleSided = doubleSided;
    mesh3dAlphaCutoff        = std::clamp(alphaCutoff, 0.f, 1.f);
    mesh3dAlphaTechnique     = alphaTechnique;
}
void Graphics::setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount) {
    mesh3dTexBombScale    = cellScale;
    mesh3dTexBombStrength = strength;
    mesh3dTexBombRot      = rotAmount;
}
void Graphics::setMesh3DParallax(float scale, float minLayers, float maxLayers) {
    mesh3dParallaxScale = scale;
    mesh3dParallaxMin   = minLayers;
    mesh3dParallaxMax   = maxLayers;
}
void Graphics::setMesh3DLodDither(float weight, bool reverse, bool enabled) {
    mesh3dLodFade = glm::vec4(std::clamp(weight, 0.f, 1.f), reverse ? 1.f : 0.f, enabled ? 1.f : 0.f, 0.f);
}
void Graphics::setMesh3DLighting(const Lighting3DPack& pack) { mesh3dLighting = pack; }
void Graphics::setCloudShadows(float strength, float worldCell, float time, float windSpeed, float windAngle,
                               float coverage, float detail) {
    mesh3dCloud     = glm::vec4(std::clamp(strength, 0.f, 1.f), std::max(worldCell, 1e-4f), time, 0.f);
    mesh3dCloudWind = glm::vec4(std::cos(windAngle) * windSpeed, std::sin(windAngle) * windSpeed,
                                std::clamp(coverage, 0.f, 1.f), std::clamp(detail, 0.f, 1.f));
}
void Graphics::setMesh3DClusteredLighting(const ClusteredLightingUpload& upload) {
    mesh3dClustered       = upload;
    mesh3dClusteredActive = upload.active;
    if (upload.active) uploadClusteredLighting(upload);
}
void Graphics::setMesh3DClusteredActive(bool active) { mesh3dClusteredActive = active; }
void Graphics::setMesh3DSSAO(float intensity) { mesh3dSsaoIntensity = std::clamp(intensity, 0.f, 1.f); }

void Graphics::uploadClusteredLighting(const ClusteredLightingUpload& upload) {
    if (!device) return;
    ClusteredStorage& st     = clusteredStorage[currentFrameSlot()];
    auto              ensure = [&](wgpu::Buffer& buf, uint64_t& cap, uint64_t need) {
        if (need == 0) need = 4;
        if (buf && cap >= need) return;
        WGPUBufferDescriptor bd{};
        bd.label            = sv("eve_clustered_ssbo");
        bd.size             = std::max(need, cap ? cap * 2 : need);
        bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        bd.mappedAtCreation = false;
        buf                 = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
        cap                 = bd.size;
    };
    const uint64_t lightsBytes  = std::max<uint64_t>(1, upload.lights.size()) * sizeof(ClusteredLightGpu);
    const uint64_t tableBytes   = std::max<uint64_t>(1, upload.clusterTable.size()) * sizeof(ClusterTableEntry);
    const uint64_t indicesBytes = std::max<uint64_t>(1, upload.lightIndices.size()) * sizeof(uint32_t);
    ensure(st.lights, st.lightsCap, lightsBytes);
    ensure(st.table, st.tableCap, tableBytes);
    ensure(st.indices, st.indicesCap, indicesBytes);

    ClusteredLightGpu zero{};
    if (!upload.lights.empty())
        queue.WriteBuffer(st.lights, 0, upload.lights.data(), lightsBytes);
    else
        queue.WriteBuffer(st.lights, 0, &zero, sizeof(zero));
    if (!upload.clusterTable.empty()) queue.WriteBuffer(st.table, 0, upload.clusterTable.data(), tableBytes);
    if (!upload.lightIndices.empty()) queue.WriteBuffer(st.indices, 0, upload.lightIndices.data(), indicesBytes);
}
void Graphics::setMesh3DLight(const glm::vec3& dir, const glm::vec3& color) {
    mesh3dLighting.lights[0].posRadius = glm::vec4(dir, 0.f);
    mesh3dLighting.lights[0].color     = glm::vec4(color, 1.f);
}
void Graphics::setMesh3DCameraPos(const glm::vec3& eye) { mesh3dCameraPos = eye; }
void Graphics::setMesh3DEnv(Texture* cube, float intensity) {
    mesh3dEnvTexture   = cube;
    mesh3dEnvIntensity = intensity;
}

void Graphics::setMesh3DEnvProbe(const glm::vec3& center, const glm::vec3& extent) {
    mesh3dEnvProbeCenter = center;
    mesh3dEnvProbeExtent = glm::max(extent, glm::vec3(0.f));
}

void Graphics::setMesh3DReflectionProbes(const ReflectionProbeUpload& upload) { mesh3dReflectionProbes = upload; }
void Graphics::setMesh3DShadows(const ShadowUpload& upload) { mesh3dShadows = upload; }
void Graphics::setMesh3DShadowReceive(bool receive) { mesh3dShadowReceive = receive; }
void Graphics::setMesh3DSkinInfluenceLimit(SkinInfluenceLimit count) {
    mesh3dSkinInfluenceLimit = static_cast<int>(count);
}

void Graphics::beginShadowPass(int cascadeIndex) {
    createShadowResources();
    shadowPassCascade = cascadeIndex;
    shadowPassDraws.clear();
}

void Graphics::drawMeshShadow(Mesh* mesh, const glm::mat4& lightMVP, bool doubleSided) {
    if (!mesh || !mesh->gpuHandle) return;
    ShadowDraw d;
    d.mesh               = mesh;
    d.mvp                = lightMVP;
    d.doubleSided        = doubleSided;
    d.skinInfluenceLimit = mesh3dSkinInfluenceLimit;
    shadowPassDraws.push_back(d);
}

void Graphics::drawMeshShadowAlpha(Mesh* mesh, const glm::mat4& lightMVP, Texture* albedo, bool doubleSided,
                                   float lodWeight, bool lodFadeReverse, bool lodDither) {
    if (!mesh || !mesh->gpuHandle) return;
    ShadowDraw d;
    d.mesh               = mesh;
    d.albedo             = albedo;
    d.mvp                = lightMVP;
    d.alphaTest          = true;
    d.doubleSided        = doubleSided;
    d.skinInfluenceLimit = mesh3dSkinInfluenceLimit;
    d.lodFade = glm::vec4(std::clamp(lodWeight, 0.f, 1.f), lodFadeReverse ? 1.f : 0.f, lodDither ? 1.f : 0.f, 0.f);
    shadowPassDraws.push_back(d);
}

void Graphics::endShadowPass() {
    if (shadowPassCascade < 0 || shadowPassCascade >= ShadowConfig::kTotalLayers) {
        shadowPassCascade = -1;
        shadowPassDraws.clear();
        return;
    }
    shadowCascadeDraws[shadowPassCascade] = shadowPassDraws;
    shadowPassDraws.clear();
    shadowPassCascade = -1;
}

// ---------------------------------------------------------------------------
// GBuffer pass
// ---------------------------------------------------------------------------

void Graphics::beginGBufferPass(int width, int height) {
    if (!device) return;
    if (!caps.supportsFiveTargetGBuffer())
        throw Exception("WebGPU: five-target GBuffer requires 40 color attachment bytes per sample");
    createGbufferResources(width, height);
    gbufferPassActive  = true;
    gbufferPassPending = true;
    gbufferPassDraws.clear();
}

void Graphics::drawMeshGBuffer(Mesh* mesh, const glm::mat4& mvp, const glm::mat4& model, float nearZ, float farZ,
                               Texture* albedo, float tintR, float tintG, float tintB, float motionX, float motionY,
                               float roughness, float metallic, float tintA) {
    if (!mesh || !mesh->gpuHandle) return;
    GbufferDraw d;
    d.mesh               = mesh;
    d.albedo             = albedo;
    d.mvp                = mvp;
    d.model              = model;
    d.nearZ              = nearZ;
    d.farZ               = farZ;
    d.tint               = glm::vec4(tintR, tintG, tintB, 1.f);
    d.motion             = glm::vec2(motionX, motionY);
    d.roughness          = roughness;
    d.metallic           = metallic;
    d.skinInfluenceLimit = mesh3dSkinInfluenceLimit;
    if (mesh3dPbrSurface) {
        auto& pbr              = d.pbrDraw.emplace();
        pbr.mesh               = mesh;
        pbr.model              = model;
        pbr.tint               = Color(tintR, tintG, tintB, tintA);
        pbr.pbrSurface         = mesh3dPbrSurface;
        pbr.gbufferClip        = glm::vec4(nearZ, farZ, motionX, motionY);
        pbr.gbufferMvp         = mvp;
        pbr.view               = mesh3dView;
        pbr.cameraPos          = mesh3dCameraPos;
        pbr.metallic           = metallic;
        pbr.roughness          = roughness;
        pbr.surfaceMode        = d.alphaTest ? SurfaceMode::Masked : SurfaceMode::Opaque;
        pbr.alphaCutoff        = mesh3dAlphaCutoff;
        pbr.doubleSided        = mesh3dSurfaceDoubleSided;
        pbr.skinInfluenceLimit = mesh3dSkinInfluenceLimit;
    }
    gbufferPassDraws.push_back(d);
}

void Graphics::drawMeshGBufferAlpha(Mesh* mesh, const glm::mat4& mvp, const glm::mat4& model, float nearZ, float farZ,
                                    Texture* albedo, float tintR, float tintG, float tintB, float motionX,
                                    float motionY, float roughness, float metallic, float tintA) {
    if (!mesh || !mesh->gpuHandle) return;
    GbufferDraw d;
    d.mesh               = mesh;
    d.albedo             = albedo;
    d.mvp                = mvp;
    d.model              = model;
    d.nearZ              = nearZ;
    d.farZ               = farZ;
    d.tint               = glm::vec4(tintR, tintG, tintB, 1.f);
    d.motion             = glm::vec2(motionX, motionY);
    d.roughness          = roughness;
    d.metallic           = metallic;
    d.alphaTest          = true;
    d.skinInfluenceLimit = mesh3dSkinInfluenceLimit;
    if (mesh3dPbrSurface) {
        auto& pbr              = d.pbrDraw.emplace();
        pbr.mesh               = mesh;
        pbr.model              = model;
        pbr.tint               = Color(tintR, tintG, tintB, tintA);
        pbr.pbrSurface         = mesh3dPbrSurface;
        pbr.gbufferClip        = glm::vec4(nearZ, farZ, motionX, motionY);
        pbr.gbufferMvp         = mvp;
        pbr.view               = mesh3dView;
        pbr.cameraPos          = mesh3dCameraPos;
        pbr.metallic           = metallic;
        pbr.roughness          = roughness;
        pbr.surfaceMode        = d.alphaTest ? SurfaceMode::Masked : SurfaceMode::Opaque;
        pbr.alphaCutoff        = mesh3dAlphaCutoff;
        pbr.doubleSided        = mesh3dSurfaceDoubleSided;
        pbr.skinInfluenceLimit = mesh3dSkinInfluenceLimit;
    }
    gbufferPassDraws.push_back(d);
}

void Graphics::endGBufferPass() {
    gbufferPassActive = false;
    // Expose the G-buffer textures (depth/normal/albedo/hwDepth/pbr/emissive) to
    // RenderControl so post passes (AO, X-ray scene depth) can sample them this frame.
    if (!gbufferSlots.empty()) {
        GbufferSlot& slot = gbufferSlots[currentFrameSlot()];
        getRenderControl()->getGBuffer()->setTargets(gbufferWidth, gbufferHeight, &slot.depthColorTex, &slot.normalTex,
                                                     &slot.albedoTex, &slot.depthTex, &slot.pbrParamsTex,
                                                     &slot.emissiveTex);
    }
}

void Graphics::beginDecalPass(int width, int height) {
    if (!device || width <= 0 || height <= 0) return;
    createDecalResources(width, height);
    if (!decalFlatAlbedo) {
        const uint8_t transparent[4]   = {0, 0, 0, 0};
        const uint8_t flatNormal[4]    = {128, 128, 255, 255};
        const uint8_t neutralParams[4] = {128, 128, 0, 255};
        decalFlatAlbedo                = newTexture(1, 1, transparent);
        decalFlatNormal                = newTexture(1, 1, flatNormal);
        decalFlatParams                = newTexture(1, 1, neutralParams);
    }
    decalPassActive  = true;
    decalPassPending = false;
    decalPassDraws.clear();
}

void Graphics::setDecalCamera(const glm::mat4& viewProj, float nearZ, float farZ) {
    (void)nearZ;
    (void)farZ;
    decalViewProj = viewProj;
}

void Graphics::drawDecal(const glm::mat4& model, Texture* albedo, Texture* normal, Texture* params,
                         const float uvRect[4], float fade, float normalStrength, float roughnessStrength,
                         float metalStrength, float emissiveStrength, int blendMode, int projectionMode,
                         float blendSharpness, float parallaxScale, float parallaxMinLayers, float parallaxMaxLayers,
                         float edgeFadeWidth) {
    if (!decalPassActive) return;
    DecalDraw draw;
    draw.model  = model;
    draw.albedo = albedo ? albedo : decalFlatAlbedo;
    draw.normal = normal ? normal : decalFlatNormal;
    draw.params = params ? params : decalFlatParams;
    if (uvRect) draw.uvRect = glm::vec4(uvRect[0], uvRect[1], uvRect[2], uvRect[3]);
    draw.fadeParams       = glm::vec4(fade, normalStrength, roughnessStrength, metalStrength);
    draw.extraParams      = glm::vec4(emissiveStrength, float(blendMode == 1), float(std::clamp(projectionMode, 0, 3)),
                                 blendSharpness > 0.f ? blendSharpness : 4.f);
    const float minLayers = std::clamp(parallaxMinLayers, 1.f, 64.f);
    draw.surfaceParams =
        glm::vec4(std::clamp(parallaxScale, 0.f, 1.f), minLayers, std::clamp(parallaxMaxLayers, minLayers, 64.f),
                  std::clamp(edgeFadeWidth, 0.f, 0.49f));
    decalPassDraws.push_back(draw);
}

void Graphics::endDecalPass() {
    if (!decalPassActive) return;
    decalPassActive  = false;
    decalPassPending = true;
}

// ---------------------------------------------------------------------------
// Voxel
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Scene color / shadow / gbuffer resources
// ---------------------------------------------------------------------------

Canvas* Graphics::newCanvas(int width, int height) {
    auto* c = new OffscreenCanvas(this, width, height);
    ownedCanvases.push_back(std::unique_ptr<eve::graphics::Canvas>(c));
    return c;
}

Canvas* Graphics::newHDRCanvas(int width, int height) {
    auto* c = new OffscreenCanvas(this, width, height, true);
    ownedCanvases.push_back(std::unique_ptr<eve::graphics::Canvas>(c));
    return c;
}

void Graphics::setCanvas(Canvas* canvas) {
    if (activeCanvas && canvas != activeCanvas) {
        flush2DToCanvas(static_cast<OffscreenCanvas*>(activeCanvas));
    }
    activeCanvas = canvas;
}

bool    Graphics::isCanvasActive() const { return activeCanvas != nullptr; }
Canvas* Graphics::getCanvas() const { return activeCanvas; }

void Graphics::flush2DToCanvas(OffscreenCanvas* canvas) {
    if (!canvas || !canvas->getTexture()) return;
    auto& uboArena = currentUboArena();
    uboArena.reset();
    auto& vtxArena = currentVertexArena();
    vtxArena.reset();

    wgpu::CommandEncoder          enc = device.CreateCommandEncoder();
    WGPURenderPassColorAttachment ca{};
    ca.view       = canvas->colorView.Get();
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp     = canvas->clearRequested ? WGPULoadOp_Clear : WGPULoadOp_Load;
    ca.storeOp    = WGPUStoreOp_Store;
    ca.clearValue = {canvas->clearColor.r, canvas->clearColor.g, canvas->clearColor.b, canvas->clearColor.a};
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount      = 1;
    rp.colorAttachments          = &ca;
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
    flush2D(pass, canvas->getWidth(), canvas->getHeight(),
            canvas->isHDR() ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8Unorm);
    pass.End();
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);
    canvas->clearRequested = false;
}

Texture* Graphics::getTexture() {
    if (activeCanvas) return activeCanvas->getTexture();
    return sceneColorTexture;
}

void Graphics::draw(eve::graphics::Graphics* gfx, const glm::mat4& matrix) const {
    (void)gfx;
    (void)matrix;
}

void Graphics::draw(Canvas* C, const glm::mat4& matrix) const {
    (void)C;
    (void)matrix;
}

void Graphics::clear(std::optional<Color> color, std::optional<int> /*stencil*/, std::optional<double> /*depth*/) {
    if (frameHad3D && activeCanvas == nullptr) return;
    clearColor      = color.value_or(backgroundColor);
    hasPendingClear = true;
    clear2DBatches();
    if (auto* canvas = dynamic_cast<OffscreenCanvas*>(activeCanvas)) {
        canvas->clear(clearColor, std::nullopt, std::nullopt);
    }
}


}  // namespace eve::graphics::webgpu
