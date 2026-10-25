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
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <vector>

namespace eve::asset_graphics {
namespace {
using MaterialData = detail::CookedMaterial;

// Compose Ry*Rx*Rz*S then write translation explicitly. Building via
// glm::translate(...)*rotate*... under GCC aarch64 -O2 has been observed to
// drop the Z translation component inside the large prefab load() body while
// X/Y survive (Linux ARM64 CI: authored z=-3 → localBounds Z unshifted).
// Keep this out-of-line so the translation stores are not reordered into the
// register-pressure region of load()'s walk.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline))
#endif
glm::mat4 nodeLocalTransform(const scene::NodeDesc& node) {
    const float tx = node.x, ty = node.y, tz = node.z;
    const float yaw = node.yaw, pitch = node.pitch, roll = node.roll;
    const float sx = node.sx, sy = node.sy, sz = node.sz;
    glm::mat4 local(1.f);
    local = glm::rotate(local, glm::radians(yaw), glm::vec3(0.f, 1.f, 0.f));
    local = glm::rotate(local, glm::radians(pitch), glm::vec3(1.f, 0.f, 0.f));
    local = glm::rotate(local, glm::radians(roll), glm::vec3(0.f, 0.f, 1.f));
    local = glm::scale(local, glm::vec3(sx, sy, sz));
    local[3][0] = tx;
    local[3][1] = ty;
    local[3][2] = tz;
    return local;
}

// Expand an AABB by the eight corners of `mesh` after an affine transform.
// Do not use glm::mat4 * glm::vec4 here: under GCC aarch64 -O2 inside load(),
// that path kept worldT.z=-3 in the matrix but still produced localBounds
// zmax=0 for mesh zmax=0 (translation term dropped on the zero-Z corners).
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline))
#endif
bool expandAabbByTransformedCorners(const glm::mat4& matrix, const std::array<float, 6>& mesh,
                                    std::array<float, 6>& aabb) {
    const float tx = matrix[3][0], ty = matrix[3][1], tz = matrix[3][2];
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = mesh[(corner & 1u) ? 3u : 0u];
        const float y = mesh[(corner & 2u) ? 4u : 1u];
        const float z = mesh[(corner & 4u) ? 5u : 2u];
        const float px = matrix[0][0] * x + matrix[1][0] * y + matrix[2][0] * z + tx;
        const float py = matrix[0][1] * x + matrix[1][1] * y + matrix[2][1] * z + ty;
        const float pz = matrix[0][2] * x + matrix[1][2] * y + matrix[2][2] * z + tz;
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return false;
        aabb[0] = std::min(aabb[0], px);
        aabb[1] = std::min(aabb[1], py);
        aabb[2] = std::min(aabb[2], pz);
        aabb[3] = std::max(aabb[3], px);
        aabb[4] = std::max(aabb[4], py);
        aabb[5] = std::max(aabb[5], pz);
    }
    return true;
}
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
    std::map<SceneObjectId, glm::mat4> transforms;
    std::map<SceneObjectId, glm::mat4> placementTransforms;
    // Iterative walk (no std::function / by-value mat4 recursion): under GCC
    // aarch64 -O2 the prior recursive lambda + glm::translate path dropped Z.
    struct WalkFrame {
        const scene::NodeDesc* node;
        glm::mat4              parent;
        glm::mat4              placedParent;
        bool                   root;
    };
    std::vector<WalkFrame> stack;
    stack.reserve(scene.value().root.children.size());
    // The template decoder owns an identity-only synthetic container; its children
    // are the authored source roots whose poses the placement convention replaces.
    for (const auto& sourceRoot : scene.value().root.children)
        stack.push_back(WalkFrame{&sourceRoot, glm::mat4(1.f), glm::mat4(1.f), true});
    while (!stack.empty()) {
        const WalkFrame frame = stack.back();
        stack.pop_back();
        const scene::NodeDesc& node = *frame.node;
        if (!node.visible) continue;
        const glm::mat4 local = nodeLocalTransform(node);
        const glm::mat4 world = frame.parent * local;
        const glm::mat4 placed =
            frame.placedParent *
            (frame.root ? glm::scale(glm::mat4(1.f), glm::vec3(node.sx, node.sy, node.sz)) : local);
        // Catch lost root translation before bounds (GCC aarch64 -O2 regression probe).
        if (frame.root && (std::fabs(world[3][0] - node.x) > 1e-4f || std::fabs(world[3][1] - node.y) > 1e-4f ||
                           std::fabs(world[3][2] - node.z) > 1e-4f))
            return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "prefab root world translation lost during hierarchy walk", {}, {},
                "asset.graphics.prefab"));
        transforms.emplace(node.persistentId, world);
        placementTransforms.emplace(node.persistentId, placed);
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
            stack.push_back(WalkFrame{&(*it), world, placed, false});
    }
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
        if (!expandAabbByTransformedCorners(matrix, bounds, out->impl_->localBounds) ||
            !expandAabbByTransformedCorners(placementMatrix, bounds, out->impl_->placementBounds))
            return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "transformed prefab bounds are not representable", {}, {},
                "asset.graphics.prefab"));
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
