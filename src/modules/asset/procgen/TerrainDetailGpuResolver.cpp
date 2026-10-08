#include "asset/procgen/TerrainDetailGpuResolver.h"

#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/procgen/TerrainDetailCard.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"

#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <map>

namespace eve::asset_procgen {
namespace {}

struct TerrainDetailGpuResolver::Impl {
    Impl(const asset::EvpackResourceReader& sourceReader, const asset::EvpackCapabilities& sourceCapabilities,
         graphics::Graphics& sourceGraphics)
        : reader(sourceReader), capabilities(sourceCapabilities), graphics(sourceGraphics), meshes(sourceGraphics),
          images(sourceGraphics) {}
    const asset::EvpackResourceReader&        reader;
    const asset::EvpackCapabilities&          capabilities;
    graphics::Graphics&                       graphics;
    asset_graphics::GraphicsMeshFactoryAdapter meshes;
    asset_graphics::GraphicsImageFactoryAdapter images;
    struct Resource {
        graphics::Mesh*                 mesh    = nullptr;
        graphics::Texture*              texture = nullptr;
        std::unique_ptr<graphics::Material> material;
        std::unique_ptr<asset_graphics::EvpackStaticPrefab> prefab;
        TerrainVegetationGpuPrototype   slots;
    };
    std::map<std::string, Resource> resources;
};

TerrainDetailGpuResolver::TerrainDetailGpuResolver(const asset::EvpackResourceReader& reader,
                                                   const asset::EvpackCapabilities& capabilities,
                                                   graphics::Graphics& graphics)
    : impl_(std::make_unique<Impl>(reader, capabilities, graphics)) {}

TerrainDetailGpuResolver::~TerrainDetailGpuResolver() {
    auto result = release();
    if (!result) std::fprintf(stderr, "terrain Detail GPU release failed: %s\n", result.error()->message().c_str());
}

TerrainDetailRuntime::TerrainDetailRuntime(const asset::EvpackResourceReader& reader,
                                           const asset::EvpackCapabilities& capabilities,
                                           graphics::Graphics& graphics)
    : resolver_(std::make_unique<TerrainDetailGpuResolver>(reader, capabilities, graphics)) {}

Result<std::unique_ptr<TerrainDetailRuntime>> TerrainDetailRuntime::load(
    const asset::EvpackResourceReader& reader, const asset::EvpackCapabilities& capabilities,
    graphics::Graphics& graphics, const AssetRef& asset, const TerrainVegetationLimits& limits) {
    EvpackInstanceSetLoader loader(reader);
    auto                    loaded = loader.load(asset, capabilities);
    if (!loaded) return Result<std::unique_ptr<TerrainDetailRuntime>>::failure(loaded.status());
    auto realization = realizeTerrainVegetation(nullptr, &loaded.value(), limits);
    if (!realization)
        return Result<std::unique_ptr<TerrainDetailRuntime>>::failure(realization.status());

    auto runtime = std::unique_ptr<TerrainDetailRuntime>(
        new TerrainDetailRuntime(reader, capabilities, graphics));
    // Resolve and validate every prototype before RenderSystem3D opens a frame. GPU uploads from the collector
    // callback can wait on the active frame and deadlock; publication is therefore deferred until preparation passes.
    auto prepared = buildTerrainVegetationGpuPlan(realization.value(), *runtime->resolver_, {});
    if (!prepared) return Result<std::unique_ptr<TerrainDetailRuntime>>::failure(prepared.status());
    runtime->renderer_ = std::make_unique<TerrainVegetationRenderer>(
        std::move(realization).takeValue(), *runtime->resolver_);
    return Result<std::unique_ptr<TerrainDetailRuntime>>::success(std::move(runtime));
}

TerrainDetailRuntime::~TerrainDetailRuntime() {
    auto result = release();
    if (!result) std::fprintf(stderr, "terrain Detail runtime release failed: %s\n",
                              result.error()->message().c_str());
}

Result<void> TerrainDetailRuntime::setFrame(TerrainVegetationGpuFrame frame) {
    if (!renderer_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "terrain Detail runtime is released",
                                                       {}, {}, "asset.procgen.terrainDetailGpu"));
    return renderer_->setFrame(frame);
}

const Diagnostic* TerrainDetailRuntime::lastSubmissionError() const {
    return renderer_ ? renderer_->lastSubmissionError() : nullptr;
}

Result<void> TerrainDetailRuntime::release() {
    renderer_.reset();
    if (!resolver_) return Result<void>::success();
    auto released = resolver_->release();
    if (!released) return released;
    resolver_.reset();
    return Result<void>::success();
}

Result<TerrainVegetationGpuPrototype> TerrainDetailGpuResolver::resolve(
    const TerrainVegetationGpuPrototypeRequest& request) {
    if (!request.detail)
        return Result<TerrainVegetationGpuPrototype>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "terrain Detail metadata is required",
                              std::string(request.prototype), {}, "asset.procgen.terrainDetailGpu"));
    const auto& detail = *request.detail;
    if (detail.resourceAsset.empty())
        return Result<TerrainVegetationGpuPrototype>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "terrain Detail resource is unresolved", detail.prototype, {},
                              "asset.procgen.terrainDetailGpu"));
    const std::string key = detail.prototype + "\n" + detail.renderMode + "\n" + detail.resourceAsset;
    if (const auto found = impl_->resources.find(key); found != impl_->resources.end()) {
        if (found->second.slots.parts.empty() &&
            (found->second.slots.meshId == graphics::kInvalidGpuDrivenSlot ||
             found->second.slots.materialId == graphics::kInvalidGpuDrivenSlot))
            return Result<TerrainVegetationGpuPrototype>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "terrain Detail resource cleanup is pending",
                                  detail.prototype, {}, "asset.procgen.terrainDetailGpu"));
        return Result<TerrainVegetationGpuPrototype>::success(found->second.slots);
    }
    auto reference = AssetRef::parse(detail.resourceAsset);
    if (!reference) return Result<TerrainVegetationGpuPrototype>::failure(reference.status());
    if (detail.usePrototypeMesh || detail.renderMode == "VertexLit") {
        auto prefab = asset_graphics::EvpackStaticPrefab::load(impl_->reader, impl_->meshes, impl_->images,
                                                               reference.value(), impl_->capabilities);
        if (!prefab) return Result<TerrainVegetationGpuPrototype>::failure(prefab.status());
        auto parts = prefab.value()->prepareGpuDriven(impl_->graphics);
        if (!parts) return Result<TerrainVegetationGpuPrototype>::failure(parts.status());
        Impl::Resource resource;
        resource.prefab = std::move(prefab).takeValue();
        resource.slots.parts.reserve(parts.value().size());
        for (const auto& source : parts.value())
            resource.slots.parts.push_back({source.localTransform, source.meshId, source.materialId, source.flags,
                                            source.lodGroupId});
        const auto slots = resource.slots;
        impl_->resources.emplace(key, std::move(resource));
        return Result<TerrainVegetationGpuPrototype>::success(slots);
    }
    auto geometry = buildTerrainDetailCard(detail);
    if (!geometry) return Result<TerrainVegetationGpuPrototype>::failure(geometry.status());
    auto mesh = impl_->meshes.uploadMesh(geometry.value().positions.data(), geometry.value().normals.data(),
                                         geometry.value().texcoords.data(),
                                         static_cast<int>(geometry.value().positions.size() / 3),
                                         geometry.value().indices.data(), static_cast<int>(geometry.value().indices.size()));
    if (!mesh) return Result<TerrainVegetationGpuPrototype>::failure(mesh.status());
    asset_graphics::EvpackImageLoader imageLoader(impl_->reader, impl_->images);
    auto image = imageLoader.load(reference.value(), impl_->capabilities);
    if (!image) {
        auto released = impl_->meshes.releaseMesh(mesh.value());
        if (!released) {
            Impl::Resource cleanup;
            cleanup.mesh = mesh.value();
            impl_->resources.emplace(key, std::move(cleanup));
            return Result<TerrainVegetationGpuPrototype>::failure(released.status());
        }
        return Result<TerrainVegetationGpuPrototype>::failure(image.status());
    }
    Impl::Resource resource;
    resource.mesh     = mesh.value();
    resource.texture  = image.value().texture;
    resource.material = std::make_unique<graphics::Material>();
    resource.material->setSurfaceMode("masked");
    resource.material->setAlphaCutoff(.5f);
    resource.material->setDoubleSided(true);
    resource.material->setCameraFacing(geometry.value().cameraFacing);
    resource.material->setAlbedoTexture(resource.texture);
    resource.material->setRoughness(1.f);
    if (!impl_->graphics.gpuDrivenMaterialUsable(resource.material.get())) {
        auto imageReleased = impl_->images.releaseImage(resource.texture);
        auto meshReleased  = impl_->meshes.releaseMesh(resource.mesh);
        if (imageReleased) resource.texture = nullptr;
        if (meshReleased) resource.mesh = nullptr;
        if (!imageReleased || !meshReleased) {
            const auto status = !imageReleased ? imageReleased.status() : meshReleased.status();
            impl_->resources.emplace(key, std::move(resource));
            return Result<TerrainVegetationGpuPrototype>::failure(status);
        }
        return Result<TerrainVegetationGpuPrototype>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "terrain Detail material is unavailable to GPU-driven rendering",
            detail.prototype, {}, "asset.procgen.terrainDetailGpu"));
    }
    resource.slots.meshId     = impl_->graphics.gpuDrivenMeshRecord(resource.mesh);
    resource.slots.materialId = impl_->graphics.gpuDrivenMaterialRecord(resource.material.get());
    if (resource.slots.meshId == graphics::kInvalidGpuDrivenSlot ||
        resource.slots.materialId == graphics::kInvalidGpuDrivenSlot) {
        auto materialReleased = impl_->graphics.gpuDrivenReleaseMaterialRecord(resource.material.get());
        auto imageReleased = impl_->images.releaseImage(resource.texture);
        auto meshReleased  = impl_->meshes.releaseMesh(resource.mesh);
        if (materialReleased) resource.material.reset();
        if (imageReleased) resource.texture = nullptr;
        if (meshReleased) resource.mesh = nullptr;
        if (!materialReleased || !imageReleased || !meshReleased) {
            const auto status = !materialReleased ? materialReleased.status()
                                : !imageReleased   ? imageReleased.status()
                                                   : meshReleased.status();
            impl_->resources.emplace(key, std::move(resource));
            return Result<TerrainVegetationGpuPrototype>::failure(status);
        }
        return Result<TerrainVegetationGpuPrototype>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "terrain Detail GPU table registration failed", detail.prototype,
                              {}, "asset.procgen.terrainDetailGpu"));
    }
    const auto slots = resource.slots;
    impl_->resources.emplace(key, std::move(resource));
    return Result<TerrainVegetationGpuPrototype>::success(slots);
}

Result<void> TerrainDetailGpuResolver::drawShadow(
    const TerrainVegetationGpuPrototypeRequest& request, const std::array<float, 16>& transform,
    const std::array<float, 16>& lightViewProjection) {
    if (!request.detail)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "terrain Detail shadow metadata is required",
                              std::string(request.prototype), {}, "asset.procgen.terrainDetailGpu"));
    const auto& detail = *request.detail;
    const std::string key = detail.prototype + "\n" + detail.renderMode + "\n" + detail.resourceAsset;
    const auto found = impl_->resources.find(key);
    // Shadow passes precede the frame's GPU collector. The first frame prepares the resource in that collector;
    // subsequent shadow passes consume it. Uploading inside an active shadow render pass can deadlock the backend.
    if (found == impl_->resources.end()) return Result<void>::success();
    if (found->second.prefab)
        return found->second.prefab->drawShadow(impl_->graphics, transform, lightViewProjection);
    if (!found->second.mesh || !found->second.texture)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "terrain Detail shadow resource cleanup is pending",
                                                       detail.prototype, {}, "asset.procgen.terrainDetailGpu"));
    const glm::mat4 lightMvp = glm::make_mat4(lightViewProjection.data()) * glm::make_mat4(transform.data());
    impl_->graphics.drawMeshShadowAlpha(found->second.mesh, lightMvp, found->second.texture, true);
    return Result<void>::success();
}

Result<void> TerrainDetailGpuResolver::release() {
    Status failed;
    for (auto iterator = impl_->resources.begin(); iterator != impl_->resources.end();) {
        if (iterator->second.material) {
            auto material = impl_->graphics.gpuDrivenReleaseMaterialRecord(iterator->second.material.get());
            if (material)
                iterator->second.material.reset();
            else
                failed = material.status();
        }
        if (iterator->second.prefab) {
            auto prefab = iterator->second.prefab->release();
            if (prefab)
                iterator->second.prefab.reset();
            else
                failed = prefab.status();
        }
        Result<void> image = Result<void>::success();
        Result<void> mesh  = Result<void>::success();
        if (iterator->second.texture) {
            image = impl_->images.releaseImage(iterator->second.texture);
            if (image) iterator->second.texture = nullptr;
        }
        if (iterator->second.mesh) {
            mesh = impl_->meshes.releaseMesh(iterator->second.mesh);
            if (mesh) iterator->second.mesh = nullptr;
        }
        if (!iterator->second.texture && !iterator->second.mesh && !iterator->second.material &&
            !iterator->second.prefab)
            iterator = impl_->resources.erase(iterator);
        else {
            failed = !image ? image.status() : mesh.status();
            ++iterator;
        }
    }
    if (failed.isFailure()) return Result<void>::failure(failed);
    return Result<void>::success();
}

}  // namespace eve::asset_procgen
