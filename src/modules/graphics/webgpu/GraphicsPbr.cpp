#include "graphics/webgpu/Graphics.h"

#include <algorithm>
#include <array>
#include <map>
#include <tuple>

#include "common/Exception.h"
#include "graphics/PbrGpuUniform.h"
#include "graphics/webgpu/PbrVariantPlan.h"
#include "graphics/webgpu/PbrVariantSource.h"
#include "graphics/webgpu/SamplerBuilder.h"

namespace eve::graphics::webgpu {
namespace {
using SamplerKey = std::array<std::uint32_t, 4>;

wgpu::AddressMode addressMode(std::uint32_t value) {
    return value == 33071   ? wgpu::AddressMode::ClampToEdge
           : value == 33648 ? wgpu::AddressMode::MirrorRepeat
                            : wgpu::AddressMode::Repeat;
}

wgpu::Sampler makePbrSampler(const wgpu::Device& device, const PbrTextureBinding& binding) {
    const auto nearestMin = binding.minFilter == 9728 || binding.minFilter == 9984 || binding.minFilter == 9986;
    return SamplerBuilder()
        .address(addressMode(binding.wrapS), addressMode(binding.wrapT), wgpu::AddressMode::Repeat)
        .filter(binding.magFilter == 9728 ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear,
                nearestMin ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear)
        .mipmap(binding.minFilter >= 9986 ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest,
                0.f, binding.minFilter >= 9984 ? 1000.f : 0.f)
        .build(device);
}

std::size_t rasterKey(BlendMode blend, bool depthWrite, PbrCullMode cull, bool alphaToCoverage) {
    return std::size_t(blend) * 16u + (depthWrite ? 8u : 0u) + (alphaToCoverage ? 4u : 0u) +
           std::size_t(cull);
}
}  // namespace

struct Graphics::PbrResources {
    struct Pipeline {
        PbrVariantPlan plan{};
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        std::uint32_t samples = 1;
        std::size_t raster = 0;
        wgpu::RenderPipeline pipeline;
        wgpu::BindGroupLayout layout;
    };
    std::vector<Pipeline> pipelines;
    std::map<SamplerKey, wgpu::Sampler> samplers;
};

void Graphics::deletePbrResources(PbrResources* resources) { delete resources; }

Result<void> Graphics::setMesh3DPbrSurface(const PbrSurface* surface) {
    if (!surface) {
        mesh3dPbrSurface.reset();
        return Result<void>::success();
    }
    auto valid = validatePbrSurface(*surface);
    if (!valid) return valid;
    const auto requireGpu = [](Texture* texture, const char* role) -> Result<void> {
        if (texture && !texture->gpuHandle)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, std::string(role) + " has no GPU resource"));
        return Result<void>::success();
    };
    const auto requireKind = [&](Texture* texture, const char* role, bool array, bool volume,
                                 std::uint32_t minimumLayers = 1u) -> Result<void> {
        if (!texture) return Result<void>::success();
        auto* gpu = gpuForTexture(texture);
        if (!gpu || gpu->isCube || gpu->isVolume != volume || (!volume && gpu->isArray != array) ||
            (array && std::uint32_t(texture->layers) < minimumLayers))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, std::string(role) + " has an incompatible texture dimension"));
        return Result<void>::success();
    };
    for (const auto& binding : surface->textures) {
        auto ready = requireGpu(binding.texture, "PBR texture");
        if (!ready) return ready;
        ready = requireKind(binding.texture, "PBR texture", false, false);
        if (!ready) return ready;
    }
    for (const auto& binding : surface->vegetationDetail.textures) {
        auto ready = requireGpu(binding.texture, "PBR detail texture");
        if (!ready) return ready;
        ready = requireKind(binding.texture, "PBR detail texture", false, false);
        if (!ready) return ready;
    }
    for (auto [texture, role] : std::array<std::pair<Texture*, const char*>, 6>{
             {{surface->vegetationExtras.texture, "PBR Extras texture"},
              {surface->vegetationColors.texture, "PBR Colors texture"},
              {surface->vegetationVertex.texture, "PBR Vertex texture"},
              {surface->vegetationMotion.texture, "PBR Motion texture"},
              {surface->vegetationMotion.noise, "PBR motion noise"},
              {surface->vegetationAlpha.noise, "PBR fade noise"}}}) {
        auto ready = requireGpu(texture, role);
        if (!ready) return ready;
    }
    for (auto [texture, role, layer] :
         std::array<std::tuple<Texture*, const char*, std::uint32_t>, 4>{
             {{surface->vegetationExtras.texture, "PBR Extras texture", surface->vegetationExtras.layer},
              {surface->vegetationColors.texture, "PBR Colors texture", surface->vegetationColors.layer},
              {surface->vegetationVertex.texture, "PBR Vertex texture", surface->vegetationVertex.layer},
              {surface->vegetationMotion.texture, "PBR Motion texture", surface->vegetationMotion.layer}}}) {
        auto ready = requireKind(texture, role, true, false, layer + 1u);
        if (!ready) return ready;
    }
    auto ready = requireKind(surface->vegetationMotion.noise, "PBR motion noise", false, false);
    if (!ready) return ready;
    ready = requireKind(surface->vegetationAlpha.noise, "PBR fade noise", false, true);
    if (!ready) return ready;
    const PbrVariantLimits limits{caps.maxSampledTexturesPerShaderStage(),
                                  caps.maxSamplersPerShaderStage()};
    auto planned = planPbrVariant(*surface, false, false, limits);
    if (!planned) return Result<void>::failure(planned.status());
    mesh3dPbrSurface = *surface;
    return Result<void>::success();
}

bool Graphics::drawPbrMesh(wgpu::RenderPassEncoder pass, WGPUTextureFormat format, bool canvasTarget,
                           Mesh3dDraw& draw, GpuMesh& mesh) {
    const auto& surface = *draw.pbrSurface;
    const bool environment = draw.environment && gpuForTexture(draw.environment) &&
                             gpuForTexture(draw.environment)->isCube;
    const bool shadows = draw.shadows.active && draw.shadowReceive && shadowDepthArray;
    const PbrVariantLimits limits{caps.maxSampledTexturesPerShaderStage(),
                                  caps.maxSamplersPerShaderStage()};
    auto planned = planPbrVariant(surface, environment, shadows, limits);
    if (!planned) throw Exception("%s", planned.error()->message().c_str());
    const auto& plan = planned.value();

    if (!pbrResources_) pbrResources_.reset(new PbrResources());
    auto& resources = *pbrResources_;
    const bool transparent = draw.surfaceMode == SurfaceMode::Transparent;
    const BlendMode blend = transparent ? draw.surfaceBlend : BlendMode::Opaque;
    const bool depthWrite = !transparent || draw.depthWrite;
    const auto cull = surface.cullMode == PbrCullMode::Inherit
                          ? (draw.doubleSided ? PbrCullMode::None : PbrCullMode::Back)
                          : surface.cullMode;
    const bool alphaToCoverage = surface.alphaToCoverage && draw.surfaceMode == SurfaceMode::Masked;
    const std::uint32_t samples = canvasTarget ? 1u : sceneColorSamples;
    const auto raster = rasterKey(blend, depthWrite, cull, alphaToCoverage);
    auto found = std::find_if(resources.pipelines.begin(), resources.pipelines.end(), [&](const auto& candidate) {
        return candidate.plan == plan && candidate.format == format && candidate.samples == samples &&
               candidate.raster == raster;
    });
    if (found == resources.pipelines.end()) {
        auto source = specializePbrVariantSources(plan);
        wgpu::ShaderSourceWGSL vertexWgsl{};
        vertexWgsl.code = source.vertex.c_str();
        wgpu::ShaderModuleDescriptor vertexDescriptor{};
        vertexDescriptor.nextInChain = &vertexWgsl;
        auto vertex = device.CreateShaderModule(&vertexDescriptor);
        wgpu::ShaderSourceWGSL fragmentWgsl{};
        fragmentWgsl.code = source.fragment.c_str();
        wgpu::ShaderModuleDescriptor fragmentDescriptor{};
        fragmentDescriptor.nextInChain = &fragmentWgsl;
        auto fragment = device.CreateShaderModule(&fragmentDescriptor);

        std::array<wgpu::VertexAttribute, 16> attributes{};
        auto attribute = [&](std::size_t index, wgpu::VertexFormat vertexFormat, std::uint64_t offset,
                             std::uint32_t location) {
            attributes[index].format = vertexFormat;
            attributes[index].offset = offset;
            attributes[index].shaderLocation = location;
        };
        attribute(0, wgpu::VertexFormat::Float32x3, 0, 0);
        attribute(1, wgpu::VertexFormat::Float32x3, 12, 1);
        attribute(2, wgpu::VertexFormat::Float32x2, 24, 2);
        attribute(3, wgpu::VertexFormat::Uint16x4, 32, 3);
        attribute(4, wgpu::VertexFormat::Float32x4, 40, 4);
        for (std::uint32_t index = 0; index < 11; ++index)
            attribute(index + 5, wgpu::VertexFormat::Float32x2, std::uint64_t(index) * 8u, index + 5);
        std::array<wgpu::VertexBufferLayout, 2> buffers{};
        buffers[0].arrayStride = sizeof(MeshVertex);
        buffers[0].attributeCount = 5;
        buffers[0].attributes = attributes.data();
        buffers[1].arrayStride = 11u * 2u * sizeof(float);
        buffers[1].attributeCount = 11;
        buffers[1].attributes = attributes.data() + 5;

        wgpu::BlendState blendState{};
        blendState.color.operation = wgpu::BlendOperation::Add;
        blendState.alpha.operation = wgpu::BlendOperation::Add;
        blendState.color.srcFactor = blend == BlendMode::Additive ? wgpu::BlendFactor::One
                                     : blend == BlendMode::Premultiplied ? wgpu::BlendFactor::One
                                                                          : wgpu::BlendFactor::SrcAlpha;
        blendState.color.dstFactor = blend == BlendMode::Multiply ? wgpu::BlendFactor::Zero
                                                                  : wgpu::BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.srcFactor = wgpu::BlendFactor::One;
        blendState.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        if (blend == BlendMode::Multiply) {
            blendState.color.srcFactor = wgpu::BlendFactor::Dst;
            blendState.color.dstFactor = wgpu::BlendFactor::Zero;
        }
        wgpu::ColorTargetState target{};
        target.format = wgpu::TextureFormat(format);
        target.blend = blend == BlendMode::Opaque ? nullptr : &blendState;
        target.writeMask = wgpu::ColorWriteMask::All;
        wgpu::FragmentState fragmentState{};
        fragmentState.module = fragment;
        fragmentState.entryPoint = "main";
        fragmentState.targetCount = 1;
        fragmentState.targets = &target;
        wgpu::DepthStencilState depth{};
        depth.format = wgpu::TextureFormat::Depth32Float;
        depth.depthWriteEnabled = depthWrite ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor descriptor{};
        descriptor.vertex.module = vertex;
        descriptor.vertex.entryPoint = "main";
        descriptor.vertex.bufferCount = buffers.size();
        descriptor.vertex.buffers = buffers.data();
        descriptor.fragment = &fragmentState;
        descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        descriptor.primitive.frontFace = wgpu::FrontFace::CCW;
        descriptor.primitive.cullMode = cull == PbrCullMode::None ? wgpu::CullMode::None
                                      : cull == PbrCullMode::Front ? wgpu::CullMode::Front
                                                                   : wgpu::CullMode::Back;
        descriptor.depthStencil = &depth;
        descriptor.multisample.count = samples;
        descriptor.multisample.mask = 0xffffffffu;
        descriptor.multisample.alphaToCoverageEnabled = alphaToCoverage;
        PbrResources::Pipeline candidate{plan, format, samples, raster};
        candidate.pipeline = device.CreateRenderPipeline(&descriptor);
        candidate.layout = candidate.pipeline.GetBindGroupLayout(0);
        resources.pipelines.push_back(std::move(candidate));
        found = std::prev(resources.pipelines.end());
    }

    float surfaceCode = float(int(draw.surfaceMode));
    if (draw.surfaceMode == SurfaceMode::Transparent && draw.surfaceBlend == BlendMode::Premultiplied)
        surfaceCode = 3.f;
    const std::uint32_t skinCount = draw.mesh->hasGpuSkinning() ? draw.mesh->getSkinPaletteCount() : 0;
    detail::PbrUniformGpu uniform = detail::buildPbrUniformGpu(
        {surface, draw.viewProj * draw.model, draw.model, draw.view, draw.cameraPos,
         glm::vec4(draw.tint.r, draw.tint.g, draw.tint.b, draw.tint.a), draw.lighting.ambient,
         draw.lighting, draw.metallic, draw.roughness, surfaceCode, draw.alphaCutoff,
         environment ? draw.environmentIntensity : 0.f, skinCount});

    const auto vertexCount = std::size_t(mesh.vertexCount);
    std::vector<float> uv(vertexCount * 22u, 0.f);
    for (std::size_t slot = 0; slot < surface.textures.size(); ++slot) {
        const auto& binding = surface.textures[slot];
        auto values = draw.mesh->texcoordSet(binding.texcoord);
        if (binding.texture && values.empty() && binding.texcoord != 0)
            throw Exception("PBR texture references missing mesh UV channel");
        if (!values.empty()) uniform.uvInfo[slot].offset = 0u;
        for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
            const auto fallback = mesh.cpuVertices[vertexIndex].uv;
            uv[vertexIndex * 22u + slot * 2u] = values.empty() ? fallback.x : values[vertexIndex * 2u];
            uv[vertexIndex * 22u + slot * 2u + 1u] = values.empty() ? fallback.y : values[vertexIndex * 2u + 1u];
        }
    }

    std::vector<float> attributes(2u, 0.f);
    if (surface.normalMode != PbrNormalMode::TangentXYZ && draw.mesh->hasImportedTangents()) {
        uniform.tangentInfo = {std::uint32_t(attributes.size()), 1u, 0u, 0u};
        const auto& tangents = draw.mesh->importedTangents();
        const auto& bitangents = draw.mesh->importedBitangents();
        for (std::size_t index = 0; index < tangents.size(); index += 3u) {
            attributes.insert(attributes.end(), tangents.begin() + index, tangents.begin() + index + 3u);
            attributes.insert(attributes.end(), bitangents.begin() + index, bitangents.begin() + index + 3u);
        }
    }
    const auto highlights = draw.mesh->motionHighlights();
    if (!highlights.empty()) {
        uniform.tangentInfo.z = std::uint32_t(attributes.size());
        uniform.tangentInfo.w = 1u;
        attributes.insert(attributes.end(), highlights.begin(), highlights.end());
    }
    const auto factors = draw.mesh->vegetationFactors();
    if (!factors.empty()) {
        uniform.vegetationInfo.x = std::uint32_t(attributes.size());
        uniform.vegetationInfo.y = 1u;
        attributes.insert(attributes.end(), factors.begin(), factors.end());
    }
    const auto deformation = draw.mesh->vegetationDeformationFactors();
    if (!deformation.empty()) {
        uniform.vertexInfo.z = std::uint32_t(attributes.size());
        uniform.vertexInfo.w = 1u;
        attributes.insert(attributes.end(), deformation.begin(), deformation.end());
    }
    if (surface.vegetationMotion.mode == PbrVegetationMotionMode::Object && deformation.empty())
        throw Exception("PBR GPU vegetation motion requires a nine-float rest-deformation stream");

    auto& arena = currentUboArena();
    const auto uniformOffset = arena.alloc(sizeof(uniform), 256);
    const auto shadowOffset = arena.alloc(sizeof(ShadowUBO), 256);
    queue.WriteBuffer(arena.buffer, uniformOffset, &uniform, sizeof(uniform));
    auto shadow = draw.shadows.ubo;
    if (!shadows) {
        shadow.bias.y = 0.f;
        shadow.splits.w = 0.f;
    }
    shadow.bias.z = draw.shadowReceive ? 1.f : 0.f;
    queue.WriteBuffer(arena.buffer, shadowOffset, &shadow, sizeof(shadow));

    const auto allocateAux = [&](const void* data, std::uint64_t bytes) {
        const auto index = arena.paletteIndex++;
        if (arena.palettes.size() <= index) arena.palettes.resize(index + 1u);
        auto& buffer = arena.palettes[index];
        if (!buffer || buffer.GetSize() < bytes) {
            wgpu::BufferDescriptor descriptor{};
            descriptor.size = std::max<std::uint64_t>(bytes, 4u);
            descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage | wgpu::BufferUsage::Vertex;
            buffer = device.CreateBuffer(&descriptor);
        }
        queue.WriteBuffer(buffer, 0, data, bytes);
        return buffer;
    };
    auto uvBuffer = allocateAux(uv.data(), uv.size() * sizeof(float));
    auto attributeBuffer = allocateAux(attributes.data(), attributes.size() * sizeof(float));
    auto skinBuffer = uploadSkinPalette(draw.mesh);

    std::vector<wgpu::BindGroupEntry> entries;
    const auto addBuffer = [&](std::uint32_t binding, const wgpu::Buffer& buffer, std::uint64_t offset,
                               std::uint64_t size) {
        wgpu::BindGroupEntry entry{};
        entry.binding = binding;
        entry.buffer = buffer;
        entry.offset = offset;
        entry.size = size;
        entries.push_back(entry);
    };
    const auto addTexture = [&](std::uint32_t binding, GpuTexture* texture) {
        wgpu::BindGroupEntry entry{};
        entry.binding = binding;
        entry.textureView = texture->view;
        entries.push_back(entry);
    };
    const auto addSampler = [&](std::uint32_t binding, const wgpu::Sampler& sampler) {
        wgpu::BindGroupEntry entry{};
        entry.binding = binding;
        entry.sampler = sampler;
        entries.push_back(entry);
    };
    addBuffer(0, arena.buffer, uniformOffset, sizeof(uniform));
    addBuffer(2, attributeBuffer, 0, attributes.size() * sizeof(float));
    addBuffer(3, skinBuffer, 0, skinBuffer.GetSize());
    addBuffer(6, arena.buffer, shadowOffset, sizeof(shadow));

    const auto samplerFor = [&](const PbrTextureBinding& binding) -> const wgpu::Sampler& {
        const SamplerKey key{binding.wrapS, binding.wrapT, binding.minFilter, binding.magFilter};
        auto sampler = resources.samplers.find(key);
        if (sampler == resources.samplers.end())
            sampler = resources.samplers.emplace(key, makePbrSampler(device, binding)).first;
        return sampler->second;
    };
    for (std::uint32_t slot = 0; slot < std::uint32_t(surface.textures.size()); ++slot) {
        if ((plan.canonicalTextureMask & (1u << slot)) == 0u) continue;
        addTexture(20u + slot * 2u, gpuForTexture(surface.textures[slot].texture));
        if (plan.canonicalSamplerRepresentatives[slot] == slot)
            addSampler(21u + slot * 2u, samplerFor(surface.textures[slot]));
    }
    for (std::uint32_t slot = 0; slot < std::uint32_t(surface.vegetationDetail.textures.size()); ++slot) {
        if ((plan.detailTextureMask & (1u << slot)) == 0u) continue;
        addTexture(42u + slot * 2u, gpuForTexture(surface.vegetationDetail.textures[slot].texture));
        if (plan.detailSamplerRepresentatives[slot] == std::uint32_t(PbrTextureSlot::Count) + slot)
            addSampler(43u + slot * 2u, samplerFor(surface.vegetationDetail.textures[slot]));
    }
    if (plan.fragmentFlags & PbrEnvironment) {
        auto* texture = gpuForTexture(draw.environment);
        addTexture(4, texture);
        addSampler(5, texture->sampler);
    }
    if (plan.fragmentFlags & PbrShadows) {
        addTexture(7, shadowDepthArray);
        addSampler(8, shadowDepthArray->sampler);
    }
    if (plan.fragmentFlags & PbrExtras) {
        auto* texture = gpuForTexture(surface.vegetationExtras.texture);
        addTexture(9, texture);
        addSampler(10, texture->sampler);
    }
    if (plan.fragmentFlags & PbrColors) {
        auto* texture = gpuForTexture(surface.vegetationColors.texture);
        addTexture(11, texture);
        addSampler(12, texture->sampler);
    }
    if (plan.fragmentFlags & PbrFadeNoise) {
        auto* texture = gpuForTexture(surface.vegetationAlpha.noise);
        addTexture(13, texture);
        addSampler(14, texture->sampler);
    }
    if (plan.vertexFlags & PbrVertexField) {
        auto* texture = gpuForTexture(surface.vegetationVertex.texture);
        addTexture(50, texture);
        addSampler(51, texture->sampler);
    }
    if (plan.vertexFlags & PbrMotionField) {
        auto* texture = gpuForTexture(surface.vegetationMotion.texture);
        addTexture(52, texture);
        addSampler(53, texture->sampler);
    }
    if (plan.vertexFlags & PbrMotionNoise) {
        auto* texture = gpuForTexture(surface.vegetationMotion.noise);
        addTexture(54, texture);
        addSampler(55, texture->sampler);
    }

    wgpu::BindGroupDescriptor bindDescriptor{};
    bindDescriptor.layout = found->layout;
    bindDescriptor.entryCount = entries.size();
    bindDescriptor.entries = entries.data();
    auto bindGroup = device.CreateBindGroup(&bindDescriptor);
    pass.SetPipeline(found->pipeline);
    pass.SetBindGroup(0, bindGroup);
    pass.SetVertexBuffer(0, mesh.vertexBuffer, 0, std::uint64_t(mesh.vertexCount) * mesh.vertexStride);
    pass.SetVertexBuffer(1, uvBuffer, 0, uv.size() * sizeof(float));
    if (mesh.indexBuffer) {
        const auto indexBytes = mesh.indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
        pass.SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0, std::uint64_t(mesh.indexCount) * indexBytes);
        pass.DrawIndexed(mesh.indexCount, 1, 0, 0, 0);
    } else {
        pass.Draw(mesh.vertexCount, 1, 0, 0);
    }
    return true;
}

}  // namespace eve::graphics::webgpu
