#include <cstdint>
#include <map>
#include <tuple>
#include "graphics/PbrGpuUniform.h"
#include "graphics/shaders/pbr_surface_frag_spv.inc"
#include "graphics/shaders/pbr_surface_vert_spv.inc"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
namespace {
using detail::PbrUniformGpu;
vk::SamplerAddressMode addressMode(uint32_t value) {
    return value == 33071   ? vk::SamplerAddressMode::eClampToEdge
           : value == 33648 ? vk::SamplerAddressMode::eMirroredRepeat
                            : vk::SamplerAddressMode::eRepeat;
}
vk::Pipeline createPbrPipeline(vkb::Device& device, vk::PipelineLayout layout, const vkb::BuiltRenderPass& pass,
                               vk::SampleCountFlagBits samples, BlendMode blend, bool depthWrite, PbrCullMode cullMode,
                               bool alphaToCoverage) {
    ShaderModulePair shaders(device, embeddedSpirv(pbr_surface_vert_spv), embeddedSpirv(pbr_surface_frag_spv));
    auto             input = vkb::VertexInputStateBuilder();
    input.addInputBinding<MeshVertex>();
    // The dedicated PBR shader reserves locations 5..15 for its eleven material
    // UV streams. MeshVertex gained a color attribute at location 5 for the
    // regular mesh pipelines, so describe only the five attributes consumed by
    // pbr_surface.vert here instead of importing the shared six-attribute list.
    input.input_attributes.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat,
                                        uint32_t(offsetof(MeshVertex, pos)));
    input.input_attributes.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat,
                                        uint32_t(offsetof(MeshVertex, normal)));
    input.input_attributes.emplace_back(2, 0, vk::Format::eR32G32Sfloat,
                                        uint32_t(offsetof(MeshVertex, uv)));
    input.input_attributes.emplace_back(3, 0, vk::Format::eR16G16B16A16Uint,
                                        uint32_t(offsetof(MeshVertex, joints)));
    input.input_attributes.emplace_back(4, 0, vk::Format::eR32G32B32A32Sfloat,
                                        uint32_t(offsetof(MeshVertex, weights)));
    // Five mesh attributes plus eleven material UV streams fit Vulkan's minimum
    // sixteen vertex attributes. Each role can select any canonical mesh UV set.
    for (uint32_t i = 0; i < 11; ++i) {
        input.input_bindings.emplace_back(i + 1, uint32_t(2 * sizeof(float)), vk::VertexInputRate::eVertex);
        input.input_attributes.emplace_back(i + 5, i + 1, vk::Format::eR32G32Sfloat, 0);
    }
    auto attachment        = makeBlendAttachment(blend);
    attachment.blendEnable = blend != BlendMode::Opaque;
    vk::PipelineColorBlendStateCreateInfo color{};
    color.attachmentCount = 1;
    color.pAttachments    = &attachment;
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples  = samples;
    multisampling.alphaToCoverageEnable = alphaToCoverage && samples != vk::SampleCountFlagBits::e1;
    return device.createPipeline()
        .useClassicPipeline(shaders.vert, shaders.frag)
        .setPipelineLayout(layout)
        .setVertexInputState(input)
        .setDynamicStatesViewportScissor()
        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                       cullMode == PbrCullMode::None ? vk::CullModeFlagBits::eNone
                                                     : (cullMode == PbrCullMode::Front ? vk::CullModeFlagBits::eFront
                                                                                       : vk::CullModeFlagBits::eBack),
                       vk::FrontFace::eCounterClockwise)
        .setMultisampler(multisampling)
        .setDepthStencil(true, depthWrite, vk::CompareOp::eLess)
        .setColorBlending(color)
        .build(pass);
}
}  // namespace
struct Graphics::PbrResources {
    struct Draw {
        vkb::GenericBuffer uniform, uv, skin, shadow;
        vk::DescriptorPool pool{};
        vk::DescriptorSet  set{};
    };
    vk::Device                                                         device{};
    vk::DescriptorSetLayout                                            setLayout{};
    vk::PipelineLayout                                                 layout{};
    std::map<std::tuple<VkRenderPass, uint32_t, size_t>, vk::Pipeline> pipelines;
    std::map<std::array<uint32_t, 4>, vk::Sampler>                     samplers;
    std::vector<std::vector<std::unique_ptr<Draw>>>                    frames;
    ~PbrResources() {
        for (auto& frame : frames)
            for (auto& draw : frame)
                if (draw->pool) device.destroyDescriptorPool(draw->pool);
        for (auto& [key, pipeline] : pipelines) device.destroyPipeline(pipeline);
        for (auto& [key, sampler] : samplers) device.destroySampler(sampler);
        if (layout) device.destroyPipelineLayout(layout);
        if (setLayout) device.destroyDescriptorSetLayout(setLayout);
    }
};
void         Graphics::deletePbrResources(PbrResources* resources) { delete resources; }
void         Graphics::destroyPbrResources() { pbrResources_.reset(); }
Result<void> Graphics::setMesh3DPbrSurface(const PbrSurface* surface) {
    if (!surface) {
        pbrSurface_.reset();
        return Result<void>::success();
    }
    auto result = validatePbrSurface(*surface);
    if (!result) return result;
    for (const auto& binding : surface->textures)
        if (binding.texture && !binding.texture->gpuHandle)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR texture has no GPU resource"));
    for (const auto& binding : surface->vegetationDetail.textures)
        if (binding.texture && !binding.texture->gpuHandle)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR detail texture has no GPU resource"));
    if (surface->vegetationExtras.texture && !surface->vegetationExtras.texture->gpuHandle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR Extras texture has no GPU resource"));
    if (surface->vegetationColors.texture && !surface->vegetationColors.texture->gpuHandle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR Colors texture has no GPU resource"));
    if (surface->vegetationVertex.texture && !surface->vegetationVertex.texture->gpuHandle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR Vertex texture has no GPU resource"));
    if (surface->vegetationMotion.texture && !surface->vegetationMotion.texture->gpuHandle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR Motion texture has no GPU resource"));
    if (surface->vegetationMotion.noise && !surface->vegetationMotion.noise->gpuHandle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR motion noise has no GPU resource"));
    if (surface->vegetationAlpha.noise && !surface->vegetationAlpha.noise->gpuHandle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "PBR vegetation fade noise has no GPU resource"));
    const auto requireKind = [](Texture* texture, const char* role, bool array, bool volume,
                                std::uint32_t minimumLayers = 1u) -> Result<void> {
        if (!texture) return Result<void>::success();
        const auto* gpu = static_cast<const GpuTexture*>(texture->gpuHandle);
        if (!gpu || gpu->isCube || gpu->isVolume != volume || (!volume && gpu->isArray != array) ||
            (array && std::uint32_t(texture->layers) < minimumLayers))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, std::string(role) + " has an incompatible texture dimension"));
        return Result<void>::success();
    };
    for (const auto& binding : surface->textures) {
        auto ready = requireKind(binding.texture, "PBR texture", false, false);
        if (!ready) return ready;
    }
    for (const auto& binding : surface->vegetationDetail.textures) {
        auto ready = requireKind(binding.texture, "PBR detail texture", false, false);
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
    pbrSurface_ = *surface;
    return Result<void>::success();
}
void Graphics::drawPbrMesh(Mesh* mesh, const glm::mat4& model, const Color& tint) {
    if (!pbrResources_) {
        pbrResources_.reset(new PbrResources());
        auto& r             = *pbrResources_;
        r.device            = device.instance;
        const auto both     = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        const auto fragment = vk::ShaderStageFlagBits::eFragment;
        const auto vertex   = vk::ShaderStageFlagBits::eVertex;
        std::array<vk::DescriptorSetLayoutBinding, 14> bindings{
            {{0, vk::DescriptorType::eUniformBuffer, 1, both},
             {1, vk::DescriptorType::eCombinedImageSampler, 11, fragment},
             {3, vk::DescriptorType::eStorageBuffer, 1, vertex},
             {4, vk::DescriptorType::eCombinedImageSampler, 1, fragment},
             {5, vk::DescriptorType::eUniformBuffer, 1, fragment},
             {6, vk::DescriptorType::eCombinedImageSampler, 1, fragment},
             {7, vk::DescriptorType::eCombinedImageSampler, 3, fragment},
             {8, vk::DescriptorType::eCombinedImageSampler, 1, fragment},
             {9, vk::DescriptorType::eCombinedImageSampler, 1, fragment},
             {10, vk::DescriptorType::eCombinedImageSampler, 1, vertex},
             {11, vk::DescriptorType::eCombinedImageSampler, 1, vertex},
             {12, vk::DescriptorType::eCombinedImageSampler, 1, vertex},
             {13, vk::DescriptorType::eCombinedImageSampler, 1, fragment},
             {2, vk::DescriptorType::eStorageBuffer, 1, vertex}}};
        vk::DescriptorSetLayoutCreateInfo info{};
        info.bindingCount = uint32_t(bindings.size());
        info.pBindings    = bindings.data();
        r.setLayout       = device->createDescriptorSetLayout(info);
        r.layout          = createPipelineLayout(device, r.setLayout);
    }
    auto& r      = *pbrResources_;
    auto& fslots = currentMesh3dFrameSlots();
    // All snapshots belong to this fenced frame slot and unique draw index.
    auto frame = currentFrameSlot();
    if (r.frames.size() <= frame) r.frames.resize(frame + 1);
    auto&      draws = r.frames[frame];
    const auto index = fslots.drawIndex++;
    while (draws.size() <= index) draws.emplace_back(std::make_unique<PbrResources::Draw>());
    auto& draw = *draws[index];
    if (!draw.pool) {
        std::array<vk::DescriptorPoolSize, 3> sizes{{{vk::DescriptorType::eUniformBuffer, 2},
                                                     {vk::DescriptorType::eStorageBuffer, 2},
                                                     {vk::DescriptorType::eCombinedImageSampler, 22}}};
        vk::DescriptorPoolCreateInfo          info{};
        info.maxSets       = 1;
        info.poolSizeCount = 3;
        info.pPoolSizes    = sizes.data();
        draw.pool          = device->createDescriptorPool(info);
        vk::DescriptorSetAllocateInfo alloc{};
        alloc.descriptorPool     = draw.pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &r.setLayout;
        draw.set                 = device->allocateDescriptorSets(alloc).front();
    }
    const auto& s = *pbrSurface_;
    PbrUniformGpu u;
    u.model    = model;
    u.mvp      = mesh3dFrameUbo.mvp * model;
    u.view     = mesh3dFrameUbo.view;
    u.camera   = glm::vec4(glm::vec3(mesh3dFrameUbo.cameraPos), s.anisotropyRotation);
    u.tint     = {tint.r, tint.g, tint.b, tint.a};
    u.ambient  = glm::vec4(glm::vec3(mesh3dLighting.ambient), s.anisotropyStrength);
    u.material = {mesh3dMetallic, mesh3dRoughness, float(int(mesh3dSurfaceMode)),
                  std::clamp(mesh3dAlphaCutoff + s.vegetationColor.globalAlphaThresholdOffset, 0.f, 1.f)};
    if (mesh3dSurfaceMode == SurfaceMode::Transparent && mesh3dSurfaceBlend == BlendMode::Premultiplied)
        u.material.z = 3;
    u.emissive              = {s.emissive[0], s.emissive[1], s.emissive[2], s.emissiveStrength};
    u.specular              = {s.specularColor[0], s.specularColor[1], s.specularColor[2], s.specularFactor};
    u.coat                  = {s.clearcoatFactor, s.clearcoatRoughness, s.clearcoatNormalScale, s.ior};
    u.colorMaskSecondary    = {s.colorMaskSecondary[0], s.colorMaskSecondary[1], s.colorMaskSecondary[2],
                               float(s.normalMode)};
    u.colorMaskParams       = {s.colorMaskEnabled ? 1.f : 0.f, s.colorMaskMin, s.colorMaskMax, s.albedoTextureStrength};
    const auto& trans       = s.translucency;
    u.translucencyColor     = {trans.color[0], trans.color[1], trans.color[2], trans.intensity};
    u.translucencyParams    = {trans.strength, trans.normalDistortion, trans.scattering, trans.direct};
    u.translucencyLighting  = {trans.ambient, trans.shadow, trans.maskAmount, trans.globalIntensity * trans.overlay};
    u.translucencyMask      = {trans.maskMinimum, trans.maskMaximum, 0, 0};
    u.motionHighlightColor  = {s.motionHighlightColor[0], s.motionHighlightColor[1], s.motionHighlightColor[2],
                               s.vegetationColor.overlaySubsurface};
    u.vegetationOverlay     = {s.vegetationColor.overlayColor[0], s.vegetationColor.overlayColor[1],
                               s.vegetationColor.overlayColor[2], s.vegetationColor.overlay};
    u.vegetationWetness     = {s.vegetationColor.wetness, s.vegetationColor.overlayNormalScale,
                               s.vegetationColor.wetnessNormalScale, s.vegetationColor.overlaySmoothness};
    u.vegetationStageParams = {s.vegetationColor.overlayVariation, s.vegetationColor.overlayProjection,
                               s.vegetationColor.vertexOcclusionAlpha, s.vegetationColor.wetnessContrast};
    u.vegetationFieldColor  = {s.vegetationColor.fieldColor[0], s.vegetationColor.fieldColor[1],
                               s.vegetationColor.fieldColor[2], s.vegetationColor.fieldColor[3]};
    u.vegetationColorParams = {s.vegetationColor.colorsCoverage, s.vegetationColor.colorsIntensity,
                               s.vegetationColor.colorsMask, s.vegetationColor.colorsVariation};
    u.vegetationOcclusionParams = {s.vegetationColor.vertexOcclusionMinimum, s.vegetationColor.vertexOcclusionMaximum,
                                   0, 0};
    u.vegetationOcclusionColor  = {s.vegetationColor.vertexOcclusionColor[0], s.vegetationColor.vertexOcclusionColor[1],
                                   s.vegetationColor.vertexOcclusionColor[2],
                                   float(s.vegetationColor.backfaceNormalMode)};
    u.vegetationGlobalMasks     = {s.vegetationColor.globalColorMaskMinimum, s.vegetationColor.globalColorMaskMaximum,
                                   s.vegetationColor.globalOverlayMaskMinimum, s.vegetationColor.globalOverlayMaskMaximum};
    const auto& d               = s.vegetationDetail;
    u.detailUv                  = {d.uvScale[0], d.uvScale[1], d.uvOffset[0], d.uvOffset[1]};
    u.detailColor               = {d.color[0], d.color[1], d.color[2], d.normalBlendValue};
    u.detailColorTwo            = {d.colorTwo[0], d.colorTwo[1], d.colorTwo[2], d.colorTwo[3]};
    u.detailValues              = {d.value, d.normalValue, d.albedoValue, d.metallicValue};
    u.detailMaterial            = {d.occlusionValue, d.smoothnessValue, d.blendMinimum, d.blendMaximum};
    u.detailMasks               = {d.maskMinimum, d.maskMaximum, d.meshMinimum, d.meshMaximum};
    u.detailInfo                = {d.uvMode, d.colorMode, d.blendMode, d.alphaMode};
    uint32_t detailPresent      = 0;
    for (size_t i = 0; i < d.textures.size(); ++i)
        if (d.textures[i].texture) detailPresent |= 1u << i;
    u.detailInfo2           = {d.maskMode, d.meshMode, d.inverseUvScale ? 1u : 0u, detailPresent};
    const auto& extras      = s.vegetationExtras;
    u.extrasCoords          = {extras.coords[0], extras.coords[1], extras.coords[2], extras.coords[3]};
    u.extrasFallback        = {extras.fallback[0], extras.fallback[1], extras.fallback[2], extras.fallback[3]};
    u.extrasUsage0          = {extras.usage[0], extras.usage[1], extras.usage[2], extras.usage[3]};
    u.extrasUsage1          = {extras.usage[4], extras.usage[5], extras.usage[6], extras.usage[7]};
    u.extrasUsage2          = {extras.usage[8], 0, 0, 0};
    u.extrasInfo            = {extras.layer, extras.usePivotPosition ? 1u : 0u, extras.texture ? 1u : 0u,
                    s.vegetationAlpha.enabled ? 1u : 0u};
    u.extrasUsage2.y        = s.vegetationAlpha.global;
    u.extrasUsage2.z        = s.vegetationAlpha.variation;
    u.extrasUsage2.w        = s.vegetationAlpha.detailFade ? 1.f : 0.f;
    u.vegetationAlphaFade   = {s.vegetationAlpha.glancing, s.vegetationAlpha.camera, s.vegetationAlpha.constant,
                               s.vegetationAlpha.noiseTiling};
    u.vegetationAlphaCamera = {s.vegetationAlpha.cameraFadeMin, s.vegetationAlpha.cameraFadeMax, 0, 0};
    u.vegetationEmission    = {s.vegetationEmission.minimum, s.vegetationEmission.maximum, s.vegetationEmission.phase,
                            s.vegetationEmission.enabled ? s.vegetationEmission.global : -1.f};
    u.vegetationGradientOne = {s.vegetationGradient.colorOne[0], s.vegetationGradient.colorOne[1],
                               s.vegetationGradient.colorOne[2],
                               s.vegetationGradient.enabled ? s.vegetationGradient.minimum : -1.f};
    u.vegetationGradientTwo = {s.vegetationGradient.colorTwo[0], s.vegetationGradient.colorTwo[1],
                               s.vegetationGradient.colorTwo[2], s.vegetationGradient.maximum};
    const auto& colors      = s.vegetationColors;
    u.colorsCoords          = {colors.coords[0], colors.coords[1], colors.coords[2], colors.coords[3]};
    u.colorsFallback        = {colors.fallback[0], colors.fallback[1], colors.fallback[2], colors.fallback[3]};
    u.colorsUsage0          = {colors.usage[0], colors.usage[1], colors.usage[2], colors.usage[3]};
    u.colorsUsage1          = {colors.usage[4], colors.usage[5], colors.usage[6], colors.usage[7]};
    u.colorsUsage2          = {colors.usage[8], 0, 0, 0};
    u.colorsInfo            = {colors.layer, colors.usePivotPosition ? 1u : 0u, colors.texture ? 1u : 0u, 0u};
    const auto& vertex      = s.vegetationVertex;
    u.vertexCoords          = {vertex.coords[0], vertex.coords[1], vertex.coords[2], vertex.coords[3]};
    u.vertexFallback        = {vertex.fallback[0], vertex.fallback[1], vertex.fallback[2], vertex.fallback[3]};
    u.vertexUsage0          = {vertex.usage[0], vertex.usage[1], vertex.usage[2], vertex.usage[3]};
    u.vertexUsage1          = {vertex.usage[4], vertex.usage[5], vertex.usage[6], vertex.usage[7]};
    u.vertexUsage2          = {vertex.usage[8], 0, 0, 0};
    u.vertexSize            = {vertex.globalSize, vertex.sizeFadeStart, vertex.sizeFadeEnd, vertex.distanceFadeBias};
    u.vertexInfo            = {vertex.layer, uint32_t(vertex.source), 0u, 0u};
    const auto& motion      = s.vegetationMotion;
    u.motionCoords          = {motion.coords[0], motion.coords[1], motion.coords[2], motion.coords[3]};
    u.motionFallback        = {motion.fallback[0], motion.fallback[1], motion.fallback[2], motion.fallback[3]};
    u.motionUsage0          = {motion.usage[0], motion.usage[1], motion.usage[2], motion.usage[3]};
    u.motionUsage1          = {motion.usage[4], motion.usage[5], motion.usage[6], motion.usage[7]};
    u.motionUsage2          = {motion.usage[8], 0, 0, 0};
    u.motionGlobal0         = {motion.globalDirection[0], motion.globalDirection[1], motion.worldOrigin[0],
                               motion.worldOrigin[1]};
    u.motionGlobal1         = {motion.worldOrigin[2], motion.dynamicMode, motion.rigidity, motion.facing};
    u.motionTime            = {float(motion.time), 0, 0, 0};
    u.motionBending         = {motion.bending, motion.bendingSpeed, motion.bendingScale, motion.bendingVariation};
    u.motionBranch          = {motion.branch, motion.rolling, motion.branchSpeed, motion.branchScale};
    u.motionBranch2         = {motion.branchVariation, motion.globalBending, motion.globalBranch, motion.globalFlutter};
    u.motionFlutter         = {motion.flutter, motion.flutterSpeed, motion.flutterScale, motion.flutterVariation};
    u.motionControl         = {motion.noiseTiling, motion.interaction, motion.interactionMask, motion.fadeDistance};
    u.motionPerspective     = {motion.perspectivePush, motion.perspectiveNoise, motion.perspectiveAngle, 0};
    u.motionInfo            = {motion.layer, uint32_t(motion.mode), 0u, 0u};
    const bool skinned = mesh->hasGpuSkinning() && mesh->getSkinPaletteCount() > 0;
    u.misc                  = {s.normalScale, s.occlusionStrength, s.unlit ? 1.f : 0.f,
              skinned ? float(mesh->getSkinPaletteCount()) : 0.f};
    for (int i = 0; i < std::min(mesh3dLighting.count, 8); ++i) u.lights[i] = mesh3dLighting.lights[i];
    u.lights[0].color.w = mesh3dEnvTexture ? mesh3dEnvIntensity : 0;
    std::map<uint32_t, size_t> offsets;
    // Binding a valid dummy stream also covers roles using the mesh vertex UV0.
    std::vector<float>                      coordinates(size_t(mesh->getVertexCount()) * 2, 0.f);
    std::array<vk::DescriptorImageInfo, 11> images;
    std::array<vk::DescriptorImageInfo, 3>  detailImages;
    for (size_t i = 0; i < 11; ++i) {
        const auto& b      = s.textures[i];
        uint32_t    offset = UINT32_MAX;
        if (b.texture) {
            auto values = mesh->texcoordSet(b.texcoord);
            if (values.empty() && b.texcoord != 0) throw Exception("PBR texture references missing mesh UV channel");
            if (!values.empty()) {
                auto found = offsets.find(b.texcoord);
                if (found == offsets.end()) {
                    size_t start = coordinates.size() / 2;
                    offsets.emplace(b.texcoord, start);
                    coordinates.insert(coordinates.end(), values.begin(), values.end());
                    offset = uint32_t(start);
                } else
                    offset = uint32_t(found->second);
            }
        }
        u.uvTransform[i] = {b.offset[0], b.offset[1], b.scale[0], b.scale[1]};
        u.uvInfo[i]      = {b.rotation, offset, b.texture ? 1.f : 0.f, b.srgbDecode ? 1.f : 0.f};
        auto* tex        = static_cast<GpuTexture*>((b.texture ? b.texture : whiteTexture)->gpuHandle);
        if (tex->isCube) throw Exception("PBR material texture must be two-dimensional");
        const std::array<uint32_t, 4> key{b.wrapS, b.wrapT, b.minFilter, b.magFilter};
        auto                          found = r.samplers.find(key);
        if (found == r.samplers.end()) {
            vk::SamplerCreateInfo info{};
            info.magFilter  = b.magFilter == 9728 ? vk::Filter::eNearest : vk::Filter::eLinear;
            info.minFilter  = (b.minFilter == 9728 || b.minFilter == 9984 || b.minFilter == 9986) ? vk::Filter::eNearest
                                                                                                  : vk::Filter::eLinear;
            info.mipmapMode = b.minFilter >= 9986 ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
            info.addressModeU = addressMode(b.wrapS);
            info.addressModeV = addressMode(b.wrapT);
            info.addressModeW = vk::SamplerAddressMode::eRepeat;
            info.maxLod       = b.minFilter >= 9984 ? VK_LOD_CLAMP_NONE : 0;
            found             = r.samplers.emplace(key, device->createSampler(info)).first;
        }
        images[i] = {found->second, tex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
    }
    for (size_t i = 0; i < d.textures.size(); ++i) {
        const auto& b   = d.textures[i];
        auto*       tex = static_cast<GpuTexture*>((b.texture ? b.texture : whiteTexture)->gpuHandle);
        if (tex->isCube) throw Exception("PBR detail texture must be two-dimensional");
        const std::array<uint32_t, 4> key{b.wrapS, b.wrapT, b.minFilter, b.magFilter};
        auto                          found = r.samplers.find(key);
        if (found == r.samplers.end()) {
            vk::SamplerCreateInfo info{};
            info.magFilter  = b.magFilter == 9728 ? vk::Filter::eNearest : vk::Filter::eLinear;
            info.minFilter  = (b.minFilter == 9728 || b.minFilter == 9984 || b.minFilter == 9986) ? vk::Filter::eNearest
                                                                                                  : vk::Filter::eLinear;
            info.mipmapMode = b.minFilter >= 9986 ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
            info.addressModeU = addressMode(b.wrapS);
            info.addressModeV = addressMode(b.wrapT);
            info.addressModeW = vk::SamplerAddressMode::eRepeat;
            info.maxLod       = b.minFilter >= 9984 ? VK_LOD_CLAMP_NONE : 0;
            found             = r.samplers.emplace(key, device->createSampler(info)).first;
        }
        detailImages[i] = {found->second, tex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
    }
    if (!extras.texture && !defaultExtrasArray) {
        const std::array<uint16_t, 4> neutral{0x3c00u, 0u, 0u, 0x3c00u};
        auto                          created = newTextureArrayRgba16f(1, 1, 1, neutral);
        if (!created.ok()) throw Exception("PBR could not create its neutral Extras array");
        defaultExtrasArray = created.value();
    }
    Texture* extrasTexture = extras.texture ? extras.texture : defaultExtrasArray;
    auto*    extrasGpu     = static_cast<GpuTexture*>(extrasTexture->gpuHandle);
    if (!extrasGpu->isArray || (extras.texture && extrasTexture->layers < 9))
        throw Exception("PBR Extras texture must be a nine-layer 2D array");
    vk::DescriptorImageInfo extrasImage{extrasGpu->sampler, extrasGpu->imageView(),
                                        vk::ImageLayout::eShaderReadOnlyOptimal};
    if (!colors.texture && !defaultColorsArray) {
        const std::array<uint16_t, 4> neutral{0x3c00u, 0x3c00u, 0x3c00u, 0u};
        auto                          created = newTextureArrayRgba16f(1, 1, 1, neutral);
        if (!created.ok()) throw Exception("PBR could not create its neutral Colors array");
        defaultColorsArray = created.value();
    }
    Texture* colorsTexture = colors.texture ? colors.texture : defaultColorsArray;
    auto*    colorsGpu     = static_cast<GpuTexture*>(colorsTexture->gpuHandle);
    if (!colorsGpu->isArray || (colors.texture && colorsTexture->layers < 9))
        throw Exception("PBR Colors texture must be a nine-layer 2D array");
    vk::DescriptorImageInfo colorsImage{colorsGpu->sampler, colorsGpu->imageView(),
                                        vk::ImageLayout::eShaderReadOnlyOptimal};
    if (!vertex.texture && !defaultVertexArray) {
        const std::array<uint16_t, 4> neutral{0u, 0u, 0u, 0x3c00u};
        auto                          created = newTextureArrayRgba16f(1, 1, 1, neutral);
        if (!created.ok()) throw Exception("PBR could not create its neutral Vertex array");
        defaultVertexArray = created.value();
    }
    Texture* vertexTexture = vertex.texture ? vertex.texture : defaultVertexArray;
    auto*    vertexGpu     = static_cast<GpuTexture*>(vertexTexture->gpuHandle);
    if (!vertexGpu->isArray || (vertex.texture && vertexTexture->layers < 9))
        throw Exception("PBR Vertex texture must be a nine-layer 2D array");
    if ((vertex.source == PbrVegetationDeformationSource::GpuFields ||
         motion.mode != PbrVegetationMotionMode::Disabled) &&
        skinned)
        throw Exception("PBR GPU vegetation deformation requires an unskinned object-mode mesh");
    vk::DescriptorImageInfo vertexImage{vertexGpu->sampler, vertexGpu->imageView(),
                                        vk::ImageLayout::eShaderReadOnlyOptimal};
    if (!motion.texture && !defaultMotionArray) {
        const std::array<uint16_t, 4> neutral{0x3c00u, 0u, 0x3800u, 0u};
        auto                          created = newTextureArrayRgba16f(1, 1, 1, neutral);
        if (!created.ok()) throw Exception("PBR could not create its neutral Motion array");
        defaultMotionArray = created.value();
    }
    Texture* motionTexture = motion.texture ? motion.texture : defaultMotionArray;
    auto*    motionGpu     = static_cast<GpuTexture*>(motionTexture->gpuHandle);
    if (!motionGpu->isArray || (motion.texture && motionTexture->layers < 9))
        throw Exception("PBR Motion texture must be a nine-layer 2D array");
    vk::DescriptorImageInfo motionImage{motionGpu->sampler, motionGpu->imageView(),
                                        vk::ImageLayout::eShaderReadOnlyOptimal};
    Texture*                noiseTexture = motion.noise ? motion.noise : whiteTexture;
    auto*                   noiseGpu     = static_cast<GpuTexture*>(noiseTexture->gpuHandle);
    if (noiseGpu->isArray || noiseGpu->isCube) throw Exception("PBR motion noise must be two-dimensional");
    vk::DescriptorImageInfo noiseImage{noiseGpu->sampler, noiseGpu->imageView(),
                                       vk::ImageLayout::eShaderReadOnlyOptimal};
    if (!defaultVegetationFadeNoise) {
        const std::array<uint8_t, 4> zero{0, 0, 0, 255};
        auto                         created = newTexture3DRgba8(1, 1, 1, zero);
        if (!created.ok()) throw Exception("PBR could not create its neutral vegetation fade volume");
        defaultVegetationFadeNoise = created.value();
    }
    Texture* fadeNoiseTexture = s.vegetationAlpha.noise ? s.vegetationAlpha.noise : defaultVegetationFadeNoise;
    auto*    fadeNoiseGpu     = static_cast<GpuTexture*>(fadeNoiseTexture->gpuHandle);
    if (!fadeNoiseGpu->isVolume) throw Exception("PBR vegetation fade noise must be three-dimensional");
    vk::DescriptorImageInfo fadeNoiseImage{fadeNoiseGpu->sampler, fadeNoiseGpu->imageView(),
                                           vk::ImageLayout::eShaderReadOnlyOptimal};
    if (coordinates.empty()) coordinates.resize(2);
    if (s.normalMode != PbrNormalMode::TangentXYZ && mesh->hasImportedTangents()) {
        const auto& tangents   = mesh->importedTangents();
        const auto& bitangents = mesh->importedBitangents();
        if (tangents.size() != size_t(mesh->getVertexCount()) * 3 || bitangents.size() != tangents.size())
            throw Exception("PBR authored tangent frame does not match mesh vertices");
        if (coordinates.size() > UINT32_MAX || tangents.size() > (UINT32_MAX - coordinates.size()) / 2)
            throw Exception("PBR tangent stream exceeds addressable range");
        u.tangentInfo = {uint32_t(coordinates.size()), 1, 0, 0};
        for (size_t i = 0; i < tangents.size(); i += 3) {
            coordinates.insert(coordinates.end(), tangents.begin() + i, tangents.begin() + i + 3);
            coordinates.insert(coordinates.end(), bitangents.begin() + i, bitangents.begin() + i + 3);
        }
    }
    const auto highlights = mesh->motionHighlights();
    if (!highlights.empty()) {
        auto valid = mesh->validateMotionHighlights(highlights);
        if (!valid) throw Exception("PBR motion highlights do not match mesh vertices");
        if (coordinates.size() > UINT32_MAX || highlights.size() > UINT32_MAX - coordinates.size())
            throw Exception("PBR highlight stream exceeds addressable range");
        u.tangentInfo.z = uint32_t(coordinates.size());
        u.tangentInfo.w = 1;
        coordinates.insert(coordinates.end(), highlights.begin(), highlights.end());
    }
    const auto vegetationFactors = mesh->vegetationFactors();
    if (!vegetationFactors.empty()) {
        auto valid = mesh->validateVegetationFactors(vegetationFactors);
        if (!valid) throw Exception("PBR vegetation factors do not match mesh vertices");
        if (coordinates.size() > UINT32_MAX || vegetationFactors.size() > UINT32_MAX - coordinates.size())
            throw Exception("PBR vegetation factor stream exceeds addressable range");
        u.vegetationInfo = {uint32_t(coordinates.size()), 1, s.vegetationColor.invertVertexOcclusion ? 1u : 0u,
                            s.vegetationColor.invertVertexOcclusionColors ? 1u : 0u};
        coordinates.insert(coordinates.end(), vegetationFactors.begin(), vegetationFactors.end());
    }
    const auto deformationFactors = mesh->vegetationDeformationFactors();
    if (!deformationFactors.empty()) {
        auto valid = mesh->validateVegetationDeformationFactors(deformationFactors);
        if (!valid) throw Exception("PBR vegetation deformation factors do not match mesh vertices");
        if (coordinates.size() > UINT32_MAX || deformationFactors.size() > UINT32_MAX - coordinates.size())
            throw Exception("PBR vegetation deformation stream exceeds addressable range");
        u.vertexInfo.z = uint32_t(coordinates.size());
        u.vertexInfo.w = 1;
        coordinates.insert(coordinates.end(), deformationFactors.begin(), deformationFactors.end());
    }
    if (motion.mode == PbrVegetationMotionMode::Object && deformationFactors.empty())
        throw Exception("PBR GPU vegetation motion requires a nine-float rest-deformation stream");
    const auto upload = [&](vkb::GenericBuffer& buffer, vk::BufferUsageFlags usage, const void* data, size_t bytes) {
        if ((usage & vk::BufferUsageFlagBits::eStorageBuffer) &&
            bytes > device.physical_device.properties.limits.maxStorageBufferRange)
            throw Exception("PBR mesh storage exceeds device maxStorageBufferRange");
        buffer.allocate(frameToken(), device, usage, bytes, kHostVisibleCoherent);
        updateRingLocal(buffer, 0, data, bytes);
    };
    upload(draw.uniform, vk::BufferUsageFlagBits::eUniformBuffer, &u, sizeof(u));
    upload(draw.uv, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
           coordinates.data(), coordinates.size() * sizeof(float));
    const glm::mat4 identity(1);
    upload(draw.skin, vk::BufferUsageFlagBits::eStorageBuffer, skinned ? mesh->skinPalette().data() : &identity[0][0],
           skinned ? mesh->skinPalette().size() * sizeof(float) : sizeof(identity));
    auto shadow = mesh3dShadows.ubo;
    if (!mesh3dShadows.active) {
        shadow.bias.y   = 0;
        shadow.splits.w = 0;
    }
    shadow.bias.z = mesh3dShadowReceive ? 1.f : 0.f;
    upload(draw.shadow, vk::BufferUsageFlagBits::eUniformBuffer, &shadow, sizeof(shadow));
    ensureDefaultEnvCubemap();
    auto* env = static_cast<GpuTexture*>((mesh3dEnvTexture ? mesh3dEnvTexture : defaultEnvCubemap)->gpuHandle);
    if (!env->isCube) throw Exception("PBR environment must be a cubemap");
    vk::DescriptorImageInfo envInfo{env->sampler, env->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo shadowInfo{shadowSampler, currentShadowArrayView(),
                                       currentShadowMap().image.currentLayout()};
    std::array<vk::DescriptorBufferInfo, 4> buffers{{{draw.uniform.buffer, 0, sizeof(u)},
                                                     {draw.skin.buffer, 0, draw.skin.size},
                                                     {draw.shadow.buffer, 0, sizeof(shadow)},
                                                     {draw.uv.buffer, 0, draw.uv.size}}};
    std::array<vk::WriteDescriptorSet, 14>  writes{};
    const std::array<uint32_t, 14>          bindings{0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 2};
    for (size_t i = 0; i < writes.size(); ++i) {
        writes[i].dstSet          = draw.set;
        writes[i].dstBinding      = bindings[i];
        writes[i].descriptorCount = 1;
    }
    for (auto [write, buffer] : std::array<std::pair<int, int>, 4>{{{0, 0}, {2, 1}, {4, 2}, {13, 3}}}) {
        writes[write].descriptorType =
            (write == 2 || write == 13) ? vk::DescriptorType::eStorageBuffer : vk::DescriptorType::eUniformBuffer;
        writes[write].pBufferInfo = &buffers[buffer];
    }
    writes[1].descriptorType = writes[3].descriptorType = writes[5].descriptorType = writes[6].descriptorType =
        writes[7].descriptorType = writes[8].descriptorType = writes[9].descriptorType = writes[10].descriptorType =
            writes[11].descriptorType = writes[12].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[1].descriptorCount = 11;
    writes[1].pImageInfo      = images.data();
    writes[3].pImageInfo      = &envInfo;
    writes[5].pImageInfo      = &shadowInfo;
    writes[6].descriptorCount                                     = 3;
    writes[6].pImageInfo                                          = detailImages.data();
    writes[7].pImageInfo                                          = &extrasImage;
    writes[8].pImageInfo                                          = &colorsImage;
    writes[9].pImageInfo                                          = &vertexImage;
    writes[10].pImageInfo                                         = &motionImage;
    writes[11].pImageInfo                                         = &noiseImage;
    writes[12].pImageInfo                                         = &fadeNoiseImage;
    device->updateDescriptorSets(writes, {});
    const auto& rp = offscreen3DPassOpen ? (offscreen3DHDRActive ? hdrOffscreen3DRenderPass : offscreen3DRenderPass)
                                         : activeScenePass();
    const auto  samples     = offscreen3DPassOpen ? vk::SampleCountFlagBits::e1 : activeSceneSamples();
    const bool  transparent = mesh3dSurfaceMode == SurfaceMode::Transparent;
    const auto  blend       = transparent ? mesh3dSurfaceBlend : BlendMode::Opaque;
    const bool  depthWrite  = !transparent || mesh3dSurfaceDepthWrite;
    const auto   resolvedCull = s.cullMode == PbrCullMode::Inherit
                                    ? (mesh3dSurfaceDoubleSided ? PbrCullMode::None : PbrCullMode::Back)
                                    : s.cullMode;
    const bool   alphaToCoverage = s.alphaToCoverage && mesh3dSurfaceMode == SurfaceMode::Masked;
    const size_t pipelineVariant =
        size_t(blend) * 12u + (depthWrite ? 6u : 0u) + (alphaToCoverage ? 3u : 0u) + size_t(resolvedCull) - 1u;
    auto        key         = std::make_tuple(VkRenderPass(rp.handle), uint32_t(samples), pipelineVariant);
    auto        found       = r.pipelines.find(key);
    if (found == r.pipelines.end())
        found = r.pipelines
                    .emplace(key, createPbrPipeline(device, r.layout, rp, samples, blend, depthWrite, resolvedCull,
                                                    alphaToCoverage))
                    .first;
    auto& cb = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, found->second);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, r.layout, 0, 1, &draw.set, 0, nullptr);
    std::array<vk::Buffer, 11> uvBuffers;
    uvBuffers.fill(draw.uv.buffer);
    std::array<vk::DeviceSize, 11> uvOffsets{};
    for (size_t i = 0; i < uvOffsets.size(); ++i)
        if (u.uvInfo[i].offset != UINT32_MAX) uvOffsets[i] = vk::DeviceSize(u.uvInfo[i].offset) * 2 * sizeof(float);
    cb.bindVertexBuffers(1, uvBuffers, uvOffsets);
    drawIndexedMesh(cb, *static_cast<GpuMesh*>(mesh->gpuHandle));
    lastMesh3dPipeline          = vk::Pipeline{};
    lastMesh3dClusteredPipeline = vk::Pipeline{};
}
}  // namespace eve::graphics::vulkan
