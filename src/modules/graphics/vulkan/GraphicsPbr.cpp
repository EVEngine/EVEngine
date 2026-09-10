#include <cstdint>
#include <map>
#include <tuple>
#include "graphics/shaders/pbr_surface_frag_spv.inc"
#include "graphics/shaders/pbr_surface_vert_spv.inc"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
namespace {
struct alignas(16) PbrUvInfo {
    float    rotation = 0;
    uint32_t offset   = UINT32_MAX;
    float    present = 0, srgb = 0;
};
struct alignas(16) PbrUniform {
    glm::mat4  mvp{1}, model{1}, view{1};
    glm::vec4  camera{}, tint{}, ambient{}, material{};
    glm::vec4  emissive{}, specular{}, coat{}, misc{};
    Light3DGpu lights[8]{};
    glm::vec4  uvTransform[11]{};
    PbrUvInfo  uvInfo[11]{};
};
static_assert(sizeof(PbrUniform) == 928);
vk::SamplerAddressMode addressMode(uint32_t value) {
    return value == 33071   ? vk::SamplerAddressMode::eClampToEdge
           : value == 33648 ? vk::SamplerAddressMode::eMirroredRepeat
                            : vk::SamplerAddressMode::eRepeat;
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
        std::array<vk::DescriptorSetLayoutBinding, 7> bindings{
            {{0, vk::DescriptorType::eUniformBuffer, 1, both},
             {1, vk::DescriptorType::eCombinedImageSampler, 11, fragment},
             {2, vk::DescriptorType::eStorageBuffer, 1, vertex},
             {3, vk::DescriptorType::eStorageBuffer, 1, vertex},
             {4, vk::DescriptorType::eCombinedImageSampler, 1, fragment},
             {5, vk::DescriptorType::eUniformBuffer, 1, fragment},
             {6, vk::DescriptorType::eCombinedImageSampler, 1, fragment}}};
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
                                                     {vk::DescriptorType::eCombinedImageSampler, 13}}};
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
    PbrUniform  u;
    u.model    = model;
    u.mvp      = mesh3dFrameUbo.mvp * model;
    u.view     = mesh3dFrameUbo.view;
    u.camera   = glm::vec4(glm::vec3(mesh3dFrameUbo.cameraPos), s.anisotropyRotation);
    u.tint     = {tint.r, tint.g, tint.b, tint.a};
    u.ambient  = glm::vec4(glm::vec3(mesh3dLighting.ambient), s.anisotropyStrength);
    u.material = {mesh3dMetallic, mesh3dRoughness, float(int(mesh3dSurfaceMode)), mesh3dAlphaCutoff};
    if (mesh3dSurfaceMode == SurfaceMode::Transparent && mesh3dSurfaceBlend == BlendMode::Premultiplied)
        u.material.z = 3;
    u.emissive         = {s.emissive[0], s.emissive[1], s.emissive[2], s.emissiveStrength};
    u.specular         = {s.specularColor[0], s.specularColor[1], s.specularColor[2], s.specularFactor};
    u.coat             = {s.clearcoatFactor, s.clearcoatRoughness, s.clearcoatNormalScale, s.ior};
    const bool skinned = mesh->hasGpuSkinning() && mesh->getSkinPaletteCount() > 0;
    u.misc             = {s.normalScale, s.occlusionStrength, s.unlit ? 1.f : 0.f,
                          skinned ? float(mesh->getSkinPaletteCount()) : 0.f};
    for (int i = 0; i < std::min(mesh3dLighting.count, 8); ++i) u.lights[i] = mesh3dLighting.lights[i];
    u.lights[0].color.w = mesh3dEnvTexture ? mesh3dEnvIntensity : 0;
    std::map<uint32_t, size_t>              offsets;
    std::vector<float>                      coordinates;
    std::array<vk::DescriptorImageInfo, 11> images;
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
    if (coordinates.empty()) coordinates.resize(2);
    const auto upload = [&](vkb::GenericBuffer& buffer, vk::BufferUsageFlagBits usage, const void* data, size_t bytes) {
        if (usage == vk::BufferUsageFlagBits::eStorageBuffer &&
            bytes > device.physical_device.properties.limits.maxStorageBufferRange)
            throw Exception("PBR mesh storage exceeds device maxStorageBufferRange");
        buffer.allocate(frameToken(), device, usage, bytes, kHostVisibleCoherent);
        updateRingLocal(buffer, 0, data, bytes);
    };
    upload(draw.uniform, vk::BufferUsageFlagBits::eUniformBuffer, &u, sizeof(u));
    upload(draw.uv, vk::BufferUsageFlagBits::eStorageBuffer, coordinates.data(), coordinates.size() * sizeof(float));
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
                                                     {draw.uv.buffer, 0, draw.uv.size},
                                                     {draw.skin.buffer, 0, draw.skin.size},
                                                     {draw.shadow.buffer, 0, sizeof(shadow)}}};
    std::array<vk::WriteDescriptorSet, 7>   writes{};
    for (uint32_t i = 0; i < 7; i++) {
        writes[i].dstSet          = draw.set;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
    }
    for (auto [binding, buff] : std::array<std::pair<int, int>, 4>{{{0, 0}, {2, 1}, {3, 2}, {5, 3}}}) {
        writes[binding].descriptorType =
            (binding == 0 || binding == 5) ? vk::DescriptorType::eUniformBuffer : vk::DescriptorType::eStorageBuffer;
        writes[binding].pBufferInfo = &buffers[buff];
    }
    writes[1].descriptorType = writes[4].descriptorType = writes[6].descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    writes[1].descriptorCount = 11;
    writes[1].pImageInfo      = images.data();
    writes[4].pImageInfo      = &envInfo;
    writes[6].pImageInfo      = &shadowInfo;
    device->updateDescriptorSets(writes, {});
    const auto& rp = offscreen3DPassOpen ? (offscreen3DHDRActive ? hdrOffscreen3DRenderPass : offscreen3DRenderPass)
                                         : activeScenePass();
    const auto  samples     = offscreen3DPassOpen ? vk::SampleCountFlagBits::e1 : activeSceneSamples();
    const bool  transparent = mesh3dSurfaceMode == SurfaceMode::Transparent;
    const auto  blend       = transparent ? mesh3dSurfaceBlend : BlendMode::Opaque;
    const bool  depthWrite  = !transparent || mesh3dSurfaceDepthWrite;
    auto        key         = std::make_tuple(VkRenderPass(rp.handle), uint32_t(samples),
                                              mesh3dPipelineIndex(blend, depthWrite, mesh3dSurfaceDoubleSided));
    auto        found       = r.pipelines.find(key);
    if (found == r.pipelines.end())
        found = r.pipelines
                    .emplace(key, createMesh3DStylePipeline(embeddedSpirv(pbr_surface_vert_spv),
                                                            embeddedSpirv(pbr_surface_frag_spv), r.layout, rp, samples,
                                                            blend, depthWrite, mesh3dSurfaceDoubleSided))
                    .first;
    auto& cb = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, found->second);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, r.layout, 0, 1, &draw.set, 0, nullptr);
    drawIndexedMesh(cb, *static_cast<GpuMesh*>(mesh->gpuHandle));
    lastMesh3dPipeline          = vk::Pipeline{};
    lastMesh3dClusteredPipeline = vk::Pipeline{};
}
}  // namespace eve::graphics::vulkan
