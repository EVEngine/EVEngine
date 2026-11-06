#include "graphics/PathSceneCapture.h"

#include "common/Diagnostic.h"
#include "common/Profile.h"
#include "graphics/IRayTracing.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/RenderTransformProjection.h"

#include <array>
#include <bit>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
namespace {
struct TextureKey {
    Texture*                texture;
    std::array<uint32_t, 9> parameters;
    bool                    operator==(const TextureKey&) const = default;
};
struct TextureHash {
    size_t operator()(const TextureKey& key) const {
        auto hash = std::hash<Texture*>{}(key.texture);
        for (auto value : key.parameters) hash ^= size_t(value) + size_t(0x9e3779b9u) + (hash << 6) + (hash >> 2);
        return hash;
    }
};
struct GeometryKey {
    Mesh*                    mesh;
    std::array<uint32_t, 16> material;
    bool                     operator==(const GeometryKey&) const = default;
};
struct GeometryHash {
    size_t operator()(const GeometryKey& key) const {
        auto hash = std::hash<Mesh*>{}(key.mesh);
        for (auto value : key.material) hash ^= size_t(value) + size_t(0x9e3779b9u) + (hash << 6) + (hash >> 2);
        return hash;
    }
};
GeometryKey geometryKey(Mesh* mesh, const PathMaterial& material) {
    const auto bits = [](float value) { return std::bit_cast<uint32_t>(value == 0.f ? 0.f : value); };
    return {mesh,
            {bits(material.baseColor.x), bits(material.baseColor.y), bits(material.baseColor.z),
             bits(material.emission.x), bits(material.emission.y), bits(material.emission.z), bits(material.roughness),
             bits(material.metallic), material.baseColorTexture, bits(material.opacity), bits(material.alphaCutoff),
             uint32_t(material.alphaMode), material.metallicRoughnessTexture, material.emissionTexture,
             material.normalTexture, bits(material.normalScale)}};
}
}  // namespace
Result<PathSceneToken> preparePathWorld(ecs::Table& world, Graphics& graphics, IRayTracing& provider) {
    EV_PROFILE_MODULE("graphics", "Path.WorldCapture");
    const auto unsupported = [](const char* message) {
        return Result<PathSceneToken>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, message, "graphics.path_scene"));
    };
    std::vector<PathGeometry>                             geometry;
    std::vector<PathMaterial>                             materials;
    std::vector<PathInstance>                             instances;
    std::vector<PathLight>                                lights;
    std::vector<PathColorTexture>                         textures;
    std::unordered_map<TextureKey, uint32_t, TextureHash> textureIndices;
    std::unordered_map<Material*, PathMaterial>           materialValues;
    const auto                                            registerTexture = [&](const PathColorTexture& input) {
        const auto bits = [](float value) { return std::bit_cast<uint32_t>(value == 0.f ? 0.f : value); };
        TextureKey key{input.sourceTexture,
                                                                  {uint32_t(input.srgbDecode), bits(input.offset.x), bits(input.offset.y), bits(input.scale.x),
                        bits(input.scale.y), bits(input.rotation), uint32_t(input.filter), uint32_t(input.addressU),
                        uint32_t(input.addressV)}};
        auto [entry, inserted] = textureIndices.try_emplace(key, uint32_t(textures.size()));
        if (inserted) textures.push_back(input);
        return entry->second;
    };
    const auto registerBasicTexture = [&](Texture* texture, bool srgbDecode) {
        if (!texture) return UINT32_MAX;
        PathColorTexture input;
        input.sourceTexture = texture;
        input.srgbDecode    = srgbDecode;
        return registerTexture(input);
    };
    // Keys live only during capture. Equal material values share geometry even
    // when authored through distinct Material objects or legacy component fields.
    std::unordered_map<GeometryKey, uint32_t, GeometryHash> geometryIndices;
    // Scope: Renderable3D descendants with Transform3D+MeshRenderer, and Light3D
    // descendants with Data. Read only; no structural mutation/events/RNG/time.
    // Phase: explicit preparation before frame recording. Service: IRayTracing.
    auto renderables = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>(world);
    for (auto it = renderables.begin(); it != renderables.end(); ++it) {
        auto [transform, renderer] = *it;
        if (!renderer->visible) continue;
        if (renderer->instances || renderer->isHair)
            return unsupported("Path world requires supported instance and hair geometry");
        const int count = renderer->partCount > 0 ? renderer->partCount : 1;
        if (count > Renderable3D::MeshRenderer::kMaxParts)
            return unsupported("Path world part count exceeds renderer storage");
        for (int part = 0; part < count; ++part) {
            auto* mesh =
                renderer->partCount > 0
                    ? renderer->parts[part].mesh
                    : (renderer->lodCount > 0 && renderer->lodMeshes[0] ? renderer->lodMeshes[0] : renderer->mesh);
            if (!mesh) continue;
            auto* material = renderer->partCount > 0 && renderer->parts[part].material ? renderer->parts[part].material
                                                                                       : renderer->material;
            PathMaterial value;
            if (material && materialValues.contains(material)) {
                value = materialValues.at(material);
            } else if (material) {
                if (material->virtualTextureMode() != MaterialVirtualTextureMode::Conventional ||
                    material->getShader() || material->getHeightTexture() ||
                    (material->surfaceMode() == SurfaceMode::Transparent &&
                     material->blendMode() != BlendMode::Alpha) ||
                    material->getShadingModel() != "pbr")
                    return unsupported("Path world material conversion is unavailable for this surface");
                value.baseColor   = {material->getTintR(), material->getTintG(), material->getTintB()};
                value.opacity     = material->getTintA();
                value.alphaCutoff = material->getAlphaCutoff();
                value.alphaMode   = material->surfaceMode() == SurfaceMode::Masked        ? PathAlphaMode::Masked
                                    : material->surfaceMode() == SurfaceMode::Transparent ? PathAlphaMode::Blend
                                                                                          : PathAlphaMode::Opaque;
                value.roughness   = material->getRoughness();
                value.metallic    = material->getMetallic();
                if (material->hasPbrSurface()) {
                    auto pbr = material->pbrSurface();
                    value.emission =
                        glm::vec3(pbr.emissive[0], pbr.emissive[1], pbr.emissive[2]) * pbr.emissiveStrength;
                    pbr.emissive         = {0.f, 0.f, 0.f};
                    pbr.emissiveStrength = 1.f;
                    value.normalScale    = pbr.normalScale;
                    pbr.normalScale      = 1.f;
                    for (auto slot : {PbrTextureSlot::BaseColor, PbrTextureSlot::MetallicRoughness,
                                      PbrTextureSlot::Emissive, PbrTextureSlot::Normal}) {
                        auto& binding = pbr.textures[size_t(slot)];
                        if (binding.texture) {
                            if (binding.texcoord != 0)
                                return unsupported(
                                    "Path world material requires UV0; additional UV sets are not captured yet");
                            PathColorTexture input;
                            input.sourceTexture = binding.texture;
                            input.srgbDecode    = binding.srgbDecode;
                            input.offset        = {binding.offset[0], binding.offset[1]};
                            input.scale         = {binding.scale[0], binding.scale[1]};
                            input.rotation      = binding.rotation;
                            input.filter =
                                binding.magFilter == 9728 ? PathTextureFilter::Nearest : PathTextureFilter::Linear;
                            const auto address = [](uint32_t wrap) {
                                return wrap == 10497
                                           ? PathTextureAddress::Repeat
                                           : (wrap == 33648 ? PathTextureAddress::Mirror : PathTextureAddress::Clamp);
                            };
                            input.addressU = address(binding.wrapS);
                            input.addressV = address(binding.wrapT);
                            auto index     = registerTexture(input);
                            if (slot == PbrTextureSlot::BaseColor) value.baseColorTexture = index;
                            if (slot == PbrTextureSlot::MetallicRoughness) value.metallicRoughnessTexture = index;
                            if (slot == PbrTextureSlot::Emissive) value.emissionTexture = index;
                            if (slot == PbrTextureSlot::Normal) value.normalTexture = index;
                        }
                        binding = {};
                    }
                    // Defaulted value comparison also covers newly added extension fields:
                    // an unimplemented non-default extension must never disappear silently.
                    if (!(pbr == PbrSurface{})) return unsupported("Path world PBR extension is not yet supported");
                } else {
                    value.baseColorTexture = registerBasicTexture(material->getAlbedoTexture(), true);
                    value.normalTexture    = registerBasicTexture(material->getNormalTexture(), false);
                }
                materialValues.emplace(material, value);
            } else {
                if (renderer->heightTexture || renderer->shader || !renderer->receiveLight)
                    return unsupported("Path world legacy material conversion is unavailable for this surface");
                value.baseColor        = {renderer->r, renderer->g, renderer->b};
                value.opacity          = renderer->a;
                value.alphaMode        = renderer->a == 1.f ? PathAlphaMode::Opaque : PathAlphaMode::Blend;
                value.roughness        = renderer->roughness;
                value.metallic         = renderer->metallic;
                value.baseColorTexture = registerBasicTexture(renderer->texture, true);
                value.normalTexture    = registerBasicTexture(renderer->normalTexture, false);
            }
            const auto [entry, inserted] =
                geometryIndices.try_emplace(geometryKey(mesh, value), uint32_t(geometry.size()));
            if (inserted) {
                PathGeometry input;
                input.sourceMesh = mesh;
                input.material   = uint32_t(materials.size());
                geometry.push_back(input);
                materials.push_back(value);
            }
            instances.push_back({entry->second, detail::modelFromTransform(*transform)});
        }
    }
    auto lightView = ecs::View<Light3D, Light3D::Data>(world);
    for (auto it = lightView.begin(); it != lightView.end(); ++it) {
        auto [light] = *it;
        if (!light->enabled) continue;
        if (light->volumetric || light->volumetricOnly ||
            (light->type != "point" && light->type != "dir" && light->type != "spot"))
            return unsupported("Path world light conversion requires point, directional or spot surface lights");
        PathLight value;
        value.kind = light->type == "dir" ? PathLightKind::Directional : PathLightKind::Point;
        if (light->type == "spot") {
            value.kind          = PathLightKind::Spot;
            value.beamDirection = {light->dx, light->dy, light->dz};
            lightSpotCosines(light->spotAngleDeg, light->spotSoftness, value.cosOuter, value.cosInner);
            value.cosInner = std::min(value.cosInner, 1.f);
        }
        value.position         = {light->x, light->y, light->z};
        value.directionToLight = {light->dx, light->dy, light->dz};
        value.strength         = glm::vec3(light->r, light->g, light->b) * light->intensity;
        lights.push_back(value);
    }
    return provider.preparePathScene(&graphics, {geometry, instances, materials, lights, textures});
}
}  // namespace eve::graphics
