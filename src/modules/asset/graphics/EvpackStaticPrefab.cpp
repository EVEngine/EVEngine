#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/CookedMaterial.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "common/Assert.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <optional>

namespace eve::asset_graphics {
namespace {
using MaterialData = detail::CookedMaterial;
}  // namespace

struct EvpackStaticPrefab::Impl {
    graphics::IMeshResourceFactory&  meshes;
    graphics::IImageResourceFactory& images;
    // Factory ABI leases: only passed back to the same factories or synchronous draw calls.
    std::map<std::string, graphics::Mesh*>       meshLeases;
    std::map<std::string, std::vector<uint32_t>> meshUvSets;
    std::map<std::string, std::array<float, 6>>  meshBounds;
    std::array<float, 6>                         localBounds{};
    std::array<float, 6>                         placementBounds{};
    bool                                         hasLocalBounds = false;
    std::optional<AssetRef>                      sourceAsset;
    PersistentId                                 sourceBuild;
    std::map<std::string, graphics::Texture*>    imageLeases;
    struct Draw {
        glm::mat4          transform;
        glm::mat4          placementTransform;
        graphics::Mesh*    mesh;
        graphics::Texture* image;
        MaterialData       material;
        bool               castShadows = true;
        bool               receiveShadows = true;
    };
    std::vector<Draw> draws;
    std::vector<std::unique_ptr<graphics::Material>> gpuMaterials;
    graphics::Graphics*                              gpuGraphics  = nullptr;
    bool                                             released     = false;
    std::size_t                                      resourcePins = 0;
};
StaticPrefabResourcePin::StaticPrefabResourcePin(std::shared_ptr<EvpackStaticPrefab> owner)
    : owner_(std::move(owner)) {}
StaticPrefabResourcePin::~StaticPrefabResourcePin() {
    EV_ASSERT(owner_->impl_->resourcePins > 0, "prefab resource pin must be balanced");
    --owner_->impl_->resourcePins;
}
Result<std::unique_ptr<StaticPrefabResourcePin>> EvpackStaticPrefab::pinResources(
    const std::shared_ptr<EvpackStaticPrefab>& owner) {
    using Output = Result<std::unique_ptr<StaticPrefabResourcePin>>;
    if (!owner || owner->impl_->released || owner->impl_->resourcePins == std::numeric_limits<std::size_t>::max())
        return Output::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "prefab resource pin requires live resources and available pin capacity"));
    auto pin = std::unique_ptr<StaticPrefabResourcePin>(new StaticPrefabResourcePin(owner));
    ++owner->impl_->resourcePins;
    return Output::success(std::move(pin));
}
struct PreparedStaticPrefabScene::Impl {
    std::shared_ptr<EvpackStaticPrefab>              owner;
    graphics::Graphics&                              graphics;
    std::vector<std::unique_ptr<graphics::Material>> materials;
    std::vector<StaticPrefabScenePart>               parts;
    bool                                             pinned = false;
};
PreparedStaticPrefabScene::PreparedStaticPrefabScene(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedStaticPrefabScene::~PreparedStaticPrefabScene() {
    auto released = release();
    if (!released.ok())
        std::fprintf(stderr, "prefab scene preparation cleanup failed: %s\n", released.status().describe().c_str());
}
const std::vector<StaticPrefabScenePart>& PreparedStaticPrefabScene::parts() const noexcept { return impl_->parts; }
Result<void>                              PreparedStaticPrefabScene::release() {
    for (const auto& material : impl_->materials) {
        auto released = impl_->graphics.gpuDrivenReleaseMaterialRecord(material.get());
        if (!released.ok()) return released;
    }
    impl_->parts.clear();
    impl_->materials.clear();
    if (impl_->pinned) {
        --impl_->owner->impl_->resourcePins;
        impl_->pinned = false;
    }
    impl_->owner.reset();
    return Result<void>::success();
}
Result<std::unique_ptr<PreparedStaticPrefabScene>> EvpackStaticPrefab::prepareScene(
    const std::shared_ptr<EvpackStaticPrefab>& owner, graphics::Graphics& graphics, PrefabRootPose rootPose) {
    if (rootPose != PrefabRootPose::Authored && rootPose != PrefabRootPose::ReplaceTranslationRotation)
        return Result<std::unique_ptr<PreparedStaticPrefabScene>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown prefab root pose"));
    if (!owner || owner->impl_->released)
        return Result<std::unique_ptr<PreparedStaticPrefabScene>>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "prefab scene preparation requires live resources"));
    if (owner->impl_->draws.size() > 65536 || owner->impl_->resourcePins == std::numeric_limits<std::size_t>::max())
        return Result<std::unique_ptr<PreparedStaticPrefabScene>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab scene preparation budget exceeded"));
    auto candidate = std::unique_ptr<PreparedStaticPrefabScene>(new PreparedStaticPrefabScene(
        std::make_unique<PreparedStaticPrefabScene::Impl>(PreparedStaticPrefabScene::Impl{owner, graphics})));
    for (const auto& draw : owner->impl_->draws) {
        if (draw.material.vegetationSurface)
            return Result<std::unique_ptr<PreparedStaticPrefabScene>>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "TVE prefab scene parts require the vegetation deformation adapter"));
        auto material = std::make_unique<graphics::Material>();
        material->setTint(draw.material.color.r, draw.material.color.g, draw.material.color.b, draw.material.color.a);
        material->setMetallic(draw.material.metallic);
        material->setRoughness(draw.material.roughness);
        material->setSurfaceMode(draw.material.transparent ? "transparent"
                                 : draw.material.masked    ? "masked"
                                                           : "opaque");
        material->setDepthWrite(!draw.material.transparent);
        material->setAlphaCutoff(draw.material.alphaCutoff);
        material->setDoubleSided(draw.material.doubleSided);
        material->setBlendMode(draw.material.blend == graphics::BlendMode::Premultiplied ? "premultiplied"
                               : draw.material.blend == graphics::BlendMode::Additive    ? "additive"
                               : draw.material.blend == graphics::BlendMode::Multiply    ? "multiply"
                                                                                         : "alpha");
        material->setAlbedoTexture(draw.image);
        auto surface = material->setPbrSurface(draw.material.surface);
        if (!surface.ok()) return Result<std::unique_ptr<PreparedStaticPrefabScene>>::failure(surface.status());
        StaticPrefabScenePart part;
        const auto&           local = rootPose == PrefabRootPose::Authored ? draw.transform : draw.placementTransform;
        std::copy(glm::value_ptr(local), glm::value_ptr(local) + 16, part.localTransform.begin());
        part.mesh           = draw.mesh;
        part.material       = material.get();
        part.castShadows    = draw.castShadows;
        part.receiveShadows = draw.receiveShadows;
        candidate->impl_->materials.push_back(std::move(material));
        candidate->impl_->parts.push_back(part);
    }
    ++owner->impl_->resourcePins;
    candidate->impl_->pinned = true;
    return Result<std::unique_ptr<PreparedStaticPrefabScene>>::success(std::move(candidate));
}
EvpackStaticPrefab::EvpackStaticPrefab(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EvpackStaticPrefab::~EvpackStaticPrefab() {
    auto result = release();
    if (!result) std::fprintf(stderr, "asset prefab release failed: %s\n", result.error()->message().c_str());
}
std::size_t                  EvpackStaticPrefab::drawCount() const noexcept { return impl_->draws.size(); }
AssetRef                     EvpackStaticPrefab::sourceAsset() const { return *impl_->sourceAsset; }
PersistentId                 EvpackStaticPrefab::sourceBuildId() const noexcept { return impl_->sourceBuild; }
Result<std::array<float, 6>> EvpackStaticPrefab::localBounds(PrefabRootPose rootPose) const {
    if (rootPose != PrefabRootPose::Authored && rootPose != PrefabRootPose::ReplaceTranslationRotation)
        return Result<std::array<float, 6>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown prefab root pose"));
    if (!impl_->hasLocalBounds)
        return Result<std::array<float, 6>>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "prefab has no enabled renderer bounds", {}, {}, "asset.graphics.prefab"));
    return Result<std::array<float, 6>>::success(rootPose == PrefabRootPose::Authored ? impl_->localBounds
                                                                                      : impl_->placementBounds);
}
Result<void> EvpackStaticPrefab::release() {
    if (impl_->resourcePins)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "prefab resource pins must be released before GPU resources"));
    impl_->released = true;
    if (impl_->gpuGraphics) {
        for (const auto& material : impl_->gpuMaterials) {
            auto released = impl_->gpuGraphics->gpuDrivenReleaseMaterialRecord(material.get());
            if (!released) return released;
        }
        impl_->gpuGraphics = nullptr;
    }
    impl_->gpuMaterials.clear();
    impl_->draws.clear();
    Status failure;
    for (auto it = impl_->meshLeases.begin(); it != impl_->meshLeases.end();) {
        auto result = impl_->meshes.releaseMesh(it->second);
        if (result)
            it = impl_->meshLeases.erase(it);
        else {
            failure = result.status();
            ++it;
        }
    }
    for (auto it = impl_->imageLeases.begin(); it != impl_->imageLeases.end();) {
        auto result = impl_->images.releaseImage(it->second);
        if (result)
            it = impl_->imageLeases.erase(it);
        else {
            failure = result.status();
            ++it;
        }
    }
    if (failure.isFailure()) return Result<void>::failure(failure);
    return Result<void>::success();
}
Result<std::vector<StaticPrefabGpuPart>> EvpackStaticPrefab::prepareGpuDriven(graphics::Graphics& gfx) {
    if (impl_->released)
        return Result<std::vector<StaticPrefabGpuPart>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "prefab resources have been released", {}, {}, "asset.graphics.prefab"));
    if (!impl_->gpuMaterials.empty())
        return Result<std::vector<StaticPrefabGpuPart>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab GPU-driven parts are already registered", {}, {},
                              "asset.graphics.prefab"));
    std::vector<std::unique_ptr<graphics::Material>> materials;
    std::vector<StaticPrefabGpuPart> parts;
    materials.reserve(impl_->draws.size());
    parts.reserve(impl_->draws.size());
    for (const auto& draw : impl_->draws) {
        if (draw.material.transparent)
            return Result<std::vector<StaticPrefabGpuPart>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "transparent prefab Detail cannot use opaque GPU submission", {}, {},
                "asset.graphics.prefab"));
        bool unsupportedImage = false;
        for (std::size_t role = 1; role < draw.material.images.size(); ++role) {
            if (role != std::size_t(graphics::PbrTextureSlot::Normal) && draw.material.images[role])
                unsupportedImage = true;
        }
        const auto conventionalBinding = [](const graphics::PbrTextureBinding& binding) {
            return binding.texcoord == 0 && binding.offset == std::array<float, 2>{0.f, 0.f} &&
                   binding.scale == std::array<float, 2>{1.f, 1.f} && binding.rotation == 0.f;
        };
        if (unsupportedImage ||
            std::any_of(draw.material.detailImages.begin(), draw.material.detailImages.end(),
                        [](const auto& image) { return image.has_value(); }) ||
            draw.material.vegetationSurface || draw.material.surface.normalScale != 1.f ||
            draw.material.surface.normalMode != graphics::PbrNormalMode::TangentXYZ ||
            !conventionalBinding(draw.material.surface.textures[0]) ||
            !conventionalBinding(draw.material.surface.textures[std::size_t(graphics::PbrTextureSlot::Normal)]))
            return Result<std::vector<StaticPrefabGpuPart>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "extended prefab material requires a GPU-driven PBR surface record",
                {}, {}, "asset.graphics.prefab"));
        auto material = std::make_unique<graphics::Material>();
        material->setTint(draw.material.color.r, draw.material.color.g, draw.material.color.b, draw.material.color.a);
        material->setMetallic(draw.material.metallic);
        material->setRoughness(draw.material.roughness);
        material->setSurfaceMode(draw.material.masked ? "masked" : "opaque");
        material->setAlphaCutoff(draw.material.alphaCutoff);
        material->setDoubleSided(draw.material.doubleSided);
        material->setAlbedoTexture(draw.image);
        material->setNormalTexture(
            draw.material.surface.textures[std::size_t(graphics::PbrTextureSlot::Normal)].texture);
        if (!gfx.gpuDrivenMaterialUsable(material.get()))
            return Result<std::vector<StaticPrefabGpuPart>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "prefab material is unavailable to GPU-driven rendering", {}, {},
                "asset.graphics.prefab"));
        materials.push_back(std::move(material));
    }
    const auto releaseRegistered = [&gfx](const std::vector<std::unique_ptr<graphics::Material>>& owned,
                                          std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            auto released = gfx.gpuDrivenReleaseMaterialRecord(owned[i].get());
            if (!released) return released;
        }
        return Result<void>::success();
    };
    for (std::size_t drawIndex = 0; drawIndex < impl_->draws.size(); ++drawIndex) {
        const auto& draw = impl_->draws[drawIndex];
        auto& material = materials[drawIndex];
        StaticPrefabGpuPart part;
        std::copy(glm::value_ptr(draw.transform), glm::value_ptr(draw.transform) + 16, part.localTransform.begin());
        part.meshId     = gfx.gpuDrivenMeshRecord(draw.mesh);
        part.materialId = gfx.gpuDrivenMaterialRecord(material.get());
        if (part.meshId == graphics::kInvalidGpuDrivenSlot || part.materialId == graphics::kInvalidGpuDrivenSlot) {
            auto cleanup = releaseRegistered(materials, drawIndex + 1);
            if (!cleanup) return Result<std::vector<StaticPrefabGpuPart>>::failure(cleanup.status());
            return Result<std::vector<StaticPrefabGpuPart>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab GPU table registration failed", {}, {},
                                  "asset.graphics.prefab"));
        }
        parts.push_back(part);
    }
    if (parts.empty())
        return Result<std::vector<StaticPrefabGpuPart>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "prefab has no enabled renderer", {}, {}, "asset.graphics.prefab"));
    impl_->gpuMaterials = std::move(materials);
    impl_->gpuGraphics = &gfx;
    return Result<std::vector<StaticPrefabGpuPart>>::success(std::move(parts));
}
Result<std::unique_ptr<EvpackStaticPrefab>> EvpackStaticPrefab::load(const asset::EvpackResourceReader& reader,
                                                                     graphics::IMeshResourceFactory&    meshes,
                                                                     graphics::IImageResourceFactory&   images,
                                                                     const AssetRef&                    ref,
                                                                     const asset::EvpackCapabilities&   caps) {
    auto scene = asset_scene::EvpackSceneTemplateLoader(reader).load(ref, caps);
    if (!scene) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(scene.status());
    auto build = reader.buildId();
    if (!build.ok()) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(build.status());
    auto out =
        std::unique_ptr<EvpackStaticPrefab>(new EvpackStaticPrefab(std::make_unique<Impl>(Impl{meshes, images})));
    out->impl_->sourceAsset = ref;
    out->impl_->sourceBuild = build.value();
    for (unsigned axis = 0; axis < 3; ++axis) {
        out->impl_->localBounds[axis]         = std::numeric_limits<float>::max();
        out->impl_->localBounds[axis + 3]     = std::numeric_limits<float>::lowest();
        out->impl_->placementBounds[axis]     = std::numeric_limits<float>::max();
        out->impl_->placementBounds[axis + 3] = std::numeric_limits<float>::lowest();
    }
    std::map<SceneObjectId, glm::mat4>                                      transforms;
    std::map<SceneObjectId, glm::mat4>                                      placementTransforms;
    std::function<void(const scene::NodeDesc&, glm::mat4, glm::mat4, bool)> walk =
        [&](const scene::NodeDesc& node, glm::mat4 parent, glm::mat4 placedParent, bool root) {
            if (!node.visible) return;
            auto local = glm::translate(glm::mat4(1), glm::vec3(node.x, node.y, node.z));
            local      = glm::rotate(local, glm::radians(node.yaw), glm::vec3(0, 1, 0));
            local      = glm::rotate(local, glm::radians(node.pitch), glm::vec3(1, 0, 0));
            local      = glm::rotate(local, glm::radians(node.roll), glm::vec3(0, 0, 1));
            local      = glm::scale(local, glm::vec3(node.sx, node.sy, node.sz));
            auto world = parent * local;
            auto placed =
                placedParent * (root ? glm::scale(glm::mat4(1), glm::vec3(node.sx, node.sy, node.sz)) : local);
            transforms.emplace(node.persistentId, world);
            placementTransforms.emplace(node.persistentId, placed);
            for (const auto& child : node.children) walk(child, world, placed, false);
        };
    // The template decoder owns an identity-only synthetic container; its children
    // are the authored source roots whose poses the placement convention replaces.
    for (const auto& sourceRoot : scene.value().root.children) walk(sourceRoot, glm::mat4(1), glm::mat4(1), true);
    EvpackGraphicsLoader meshLoader(reader, meshes);
    EvpackImageLoader    imageLoader(reader, images);
    for (const auto& binding : scene.value().renderers) {
        if (!binding.enabled || !transforms.contains(binding.object)) continue;
        auto parameters = detail::readCookedMaterial(reader, binding.material, caps);
        if (!parameters) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(parameters.status());
        const auto meshKey = binding.mesh.format();
        if (!out->impl_->meshLeases.contains(meshKey)) {
            auto loaded = meshLoader.loadMesh(binding.mesh, caps, {}, 0, true);
            if (!loaded) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(loaded.status());
            out->impl_->meshLeases.emplace(meshKey, loaded.value().mesh);
            out->impl_->meshUvSets.emplace(meshKey, loaded.value().availableTexcoordSets);
            out->impl_->meshBounds.emplace(meshKey, loaded.value().localBounds);
        }
        graphics::Texture* image = nullptr;
        for (size_t role = 0; role < 11; role++) {
            if (!parameters.value().images[role]) continue;
            const auto& sets     = out->impl_->meshUvSets.at(meshKey);
            auto        selected = parameters.value().surface.textures[role].texcoord;
            if (std::find(sets.begin(), sets.end(), selected) == sets.end())
                return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "material references missing mesh UV channel",
                                      {}, {}, "asset.graphics.prefab"));
            const auto& ref      = *parameters.value().images[role];
            const auto  imageKey = ref.format();
            if (!out->impl_->imageLeases.contains(imageKey)) {
                auto loaded = imageLoader.load(ref, caps);
                if (!loaded) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(loaded.status());
                out->impl_->imageLeases.emplace(imageKey, loaded.value().texture);
            }
            parameters.value().surface.textures[role].texture = out->impl_->imageLeases.at(imageKey);
        }
        for (size_t role = 0; role < 3; ++role) {
            if (!parameters.value().detailImages[role]) continue;
            const auto& ref      = *parameters.value().detailImages[role];
            const auto  imageKey = ref.format();
            if (!out->impl_->imageLeases.contains(imageKey)) {
                auto loaded = imageLoader.load(ref, caps);
                if (!loaded) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(loaded.status());
                out->impl_->imageLeases.emplace(imageKey, loaded.value().texture);
            }
            parameters.value().surface.vegetationDetail.textures[role].texture = out->impl_->imageLeases.at(imageKey);
        }
        auto validatedSurface = graphics::validatePbrSurface(parameters.value().surface);
        if (!validatedSurface) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(validatedSurface.status());
        image                       = parameters.value().surface.textures[0].texture;
        const auto& bounds          = out->impl_->meshBounds.at(meshKey);
        const auto& matrix          = transforms.at(binding.object);
        const auto& placementMatrix = placementTransforms.at(binding.object);
        for (unsigned corner = 0; corner < 8; ++corner) {
            const auto p      = matrix * glm::vec4(bounds[(corner & 1) ? 3 : 0], bounds[(corner & 2) ? 4 : 1],
                                                   bounds[(corner & 4) ? 5 : 2], 1.f);
            const auto placed = placementMatrix * glm::vec4(bounds[(corner & 1) ? 3 : 0], bounds[(corner & 2) ? 4 : 1],
                                                            bounds[(corner & 4) ? 5 : 2], 1.f);
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(p[axis]) || !std::isfinite(placed[axis]))
                    return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "transformed prefab bounds are not representable", {}, {},
                        "asset.graphics.prefab"));
                out->impl_->localBounds[axis]         = std::min(out->impl_->localBounds[axis], p[axis]);
                out->impl_->localBounds[axis + 3]     = std::max(out->impl_->localBounds[axis + 3], p[axis]);
                out->impl_->placementBounds[axis]     = std::min(out->impl_->placementBounds[axis], placed[axis]);
                out->impl_->placementBounds[axis + 3] = std::max(out->impl_->placementBounds[axis + 3], placed[axis]);
            }
        }
        out->impl_->hasLocalBounds = true;
        out->impl_->draws.push_back({transforms.at(binding.object), placementTransforms.at(binding.object),
                                     out->impl_->meshLeases.at(meshKey), image, std::move(parameters).takeValue(),
                                     binding.castShadows, binding.receiveShadows});
    }
    return Result<std::unique_ptr<EvpackStaticPrefab>>::success(std::move(out));
}
Result<void> EvpackStaticPrefab::draw(graphics::Graphics& gfx, const std::array<float, 16>& transform,
                                      const std::array<float, 16>& cameraView) const {
    if (impl_->released)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "prefab resources have been released", {}, {}, "asset.graphics.prefab"));
    for (float value : transform)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "nonfinite instance transform", {}, {}, "asset.graphics.prefab"));
    for (float value : cameraView)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "nonfinite camera view", {},
                                                           {}, "asset.graphics.prefab"));
    const auto instance = glm::make_mat4(transform.data());
    const auto               view     = glm::make_mat4(cameraView.data());
    std::vector<std::size_t> order(impl_->draws.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& left  = impl_->draws[a];
        const auto& right = impl_->draws[b];
        if (left.material.transparent != right.material.transparent) return !left.material.transparent;
        return left.material.transparent &&
               (view * instance * left.transform)[3].z < (view * instance * right.transform)[3].z;
    });
    for (auto index : order) {
        const auto& draw = impl_->draws[index];
        gfx.setMesh3DSurface(draw.material.transparent ? graphics::SurfaceMode::Transparent
                             : draw.material.masked    ? graphics::SurfaceMode::Masked
                                                       : graphics::SurfaceMode::Opaque,
                             draw.material.blend, !draw.material.transparent, draw.material.doubleSided,
                             draw.material.alphaCutoff, "cutoff");
        auto bound = gfx.setMesh3DPbrSurface(&draw.material.surface);
        if (!bound) return bound;
        gfx.setMesh3DMaterial(draw.material.metallic, draw.material.roughness);
        gfx.setMesh3DNormalTexture(nullptr);
        gfx.setMesh3DHeightTexture(nullptr);
        gfx.setMesh3DVirtualTexture(false, 1, 1, 1, 1, 0.f);
        gfx.setMesh3DTexCellBomb(1.f, 0.f, 0.f);
        gfx.setMesh3DParallax(0.f);
        gfx.setMesh3DShadowReceive(draw.receiveShadows);
        auto color = draw.material.color;
        gfx.drawMesh(draw.mesh, instance * draw.transform, draw.image, color);
    }
    return gfx.setMesh3DPbrSurface(nullptr);
}

Result<void> EvpackStaticPrefab::drawShadow(graphics::Graphics& gfx,
                                            const std::array<float, 16>& transform,
                                            const std::array<float, 16>& lightViewProjection) const {
    if (impl_->released)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "prefab resources have been released", {}, {}, "asset.graphics.prefab"));
    for (float value : transform)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "nonfinite instance transform", {}, {}, "asset.graphics.prefab"));
    for (float value : lightViewProjection)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "nonfinite shadow view projection", {}, {}, "asset.graphics.prefab"));
    const glm::mat4 instance = glm::make_mat4(transform.data());
    const glm::mat4 lightVP  = glm::make_mat4(lightViewProjection.data());
    for (const auto& draw : impl_->draws) {
        if (!draw.castShadows || draw.material.transparent) continue;
        const glm::mat4 lightMvp = lightVP * instance * draw.transform;
        const bool doubleSided = draw.material.surface.cullMode == graphics::PbrCullMode::None ||
                                 (draw.material.surface.cullMode == graphics::PbrCullMode::Inherit &&
                                  draw.material.doubleSided);
        if (draw.material.masked)
            gfx.drawMeshShadowAlpha(draw.mesh, lightMvp, draw.image, doubleSided);
        else
            gfx.drawMeshShadow(draw.mesh, lightMvp, doubleSided);
    }
    return Result<void>::success();
}
}  // namespace eve::asset_graphics
