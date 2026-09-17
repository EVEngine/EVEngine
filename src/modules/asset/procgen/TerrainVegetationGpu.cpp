#include "asset/procgen/TerrainVegetationGpu.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <map>
#include <algorithm>
#include <cmath>

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.procgen.terrainVegetationGpu"));
}

}  // namespace

Result<TerrainVegetationGpuPlan> buildTerrainVegetationGpuPlan(const TerrainVegetationRealization& realization,
                                                               ITerrainVegetationGpuResolver& resolver,
                                                               const TerrainVegetationGpuFrame& frame) {
    if (!std::isfinite(frame.timeSeconds) || std::abs(frame.timeSeconds) > 1.0e12)
        return failure<TerrainVegetationGpuPlan>(DiagnosticCode::InvalidArgument,
                                                 "terrain vegetation frame time is invalid");
    TerrainVegetationGpuPlan result;
    result.instances.reserve(realization.instances.size());
    std::map<std::string_view, const RuntimeInstancePrototype*, std::less<>> prototypeMetadata;
    for (const auto& prototype : realization.prototypes)
        if (prototype.prototype.empty() || !prototypeMetadata.emplace(prototype.prototype, &prototype).second)
            return failure<TerrainVegetationGpuPlan>(
                DiagnosticCode::Conflict, "terrain vegetation prototype metadata is invalid", prototype.prototype);
    std::size_t expectedFirst = 0;
    for (const auto& bucket : realization.buckets) {
        if (bucket.prototype.empty() || bucket.firstInstance != expectedFirst || bucket.instanceCount == 0 ||
            std::uint64_t(bucket.firstInstance) + bucket.instanceCount > realization.instances.size())
            return failure<TerrainVegetationGpuPlan>(DiagnosticCode::InvalidArgument,
                                                     "terrain vegetation bucket layout is invalid", bucket.prototype);
        const auto metadata = prototypeMetadata.find(bucket.prototype);
        auto       prototype =
            resolver.resolve({bucket.prototype, metadata == prototypeMetadata.end() ? nullptr : metadata->second});
        if (!prototype) return Result<TerrainVegetationGpuPlan>::failure(prototype.status());
        if (prototype.value().parts.empty() &&
            (prototype.value().meshId == graphics::kInvalidGpuDrivenSlot ||
             prototype.value().materialId == graphics::kInvalidGpuDrivenSlot))
            return failure<TerrainVegetationGpuPlan>(
                DiagnosticCode::NotFound, "vegetation prototype has no GPU mesh or material", bucket.prototype);
        for (const auto& part : prototype.value().parts)
            if (part.meshId == graphics::kInvalidGpuDrivenSlot || part.materialId == graphics::kInvalidGpuDrivenSlot)
                return failure<TerrainVegetationGpuPlan>(DiagnosticCode::NotFound,
                                                         "vegetation prefab part has no GPU mesh or material",
                                                         bucket.prototype);
        for (std::uint32_t offset = 0; offset < bucket.instanceCount; ++offset) {
            const auto& source = realization.instances[bucket.firstInstance + offset];
            if (source.prototype != bucket.prototype)
                return failure<TerrainVegetationGpuPlan>(
                    DiagnosticCode::Conflict, "vegetation bucket does not match instance prototype", bucket.prototype);
            const glm::quat rotation(source.rotation[3], source.rotation[0], source.rotation[1], source.rotation[2]);
            const glm::mat4 model =
                glm::translate(glm::mat4(1.f), {source.position[0], source.position[1], source.position[2]}) *
                glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1.f), {source.scale[0], source.scale[1], source.scale[2]});
            glm::vec4 instanceColor(1.f);
            if (metadata != prototypeMetadata.end() && !metadata->second->useInstancing) {
                const auto& detail = *metadata->second;
                const float span = detail.maxHeight - detail.minHeight;
                const float healthy = span > 0.f ? std::clamp((source.scale[1] - detail.minHeight) / span, 0.f, 1.f) : 1.f;
                for (std::size_t component = 0; component < 4; ++component)
                    instanceColor[component] =
                        detail.dryColor[component] + (detail.healthyColor[component] - detail.dryColor[component]) * healthy;
            }
            glm::vec4 terrainWave{};
            glm::vec4 terrainWaveTint(1.f);
            if (metadata != prototypeMetadata.end() && !metadata->second->usePrototypeMesh) {
                terrainWave = {static_cast<float>(frame.timeSeconds * realization.wavingGrassSpeed),
                               realization.wavingGrassAmount, realization.wavingGrassStrength, 1.f};
                terrainWaveTint = glm::make_vec4(realization.wavingGrassTint.data());
            }
            if (prototype.value().parts.empty()) {
                graphics::GpuInstance instance{};
                instance.model      = model;
                instance.meshId     = prototype.value().meshId;
                instance.materialId = prototype.value().materialId;
                instance.flags      = prototype.value().flags;
                instance.lodGroupId = prototype.value().lodGroupId;
                instance.color      = instanceColor;
                instance.terrainWave = terrainWave;
                instance.terrainWaveTint = terrainWaveTint;
                result.instances.push_back(instance);
            } else {
                for (const auto& part : prototype.value().parts) {
                    graphics::GpuInstance instance{};
                    instance.model      = model * glm::make_mat4(part.localTransform.data());
                    instance.meshId     = part.meshId;
                    instance.materialId = part.materialId;
                    instance.flags      = part.flags;
                    instance.lodGroupId = part.lodGroupId;
                    instance.color      = instanceColor;
                    instance.terrainWave = terrainWave;
                    instance.terrainWaveTint = terrainWaveTint;
                    result.instances.push_back(instance);
                }
            }
        }
        expectedFirst += bucket.instanceCount;
    }
    if (expectedFirst != realization.instances.size())
        return failure<TerrainVegetationGpuPlan>(DiagnosticCode::InvalidArgument,
                                                 "terrain vegetation buckets do not cover every instance");
    return Result<TerrainVegetationGpuPlan>::success(std::move(result));
}

Result<void> submitTerrainVegetation(graphics::Graphics& graphics, const TerrainVegetationRealization& realization,
                                     ITerrainVegetationGpuResolver& resolver,
                                     const TerrainVegetationGpuFrame& frame) {
    auto plan = buildTerrainVegetationGpuPlan(realization, resolver, frame);
    if (!plan) return Result<void>::failure(plan.status());
    if (plan.value().instances.empty()) return Result<void>::success();
    if (!graphics.gpuDrivenSubmitOpaque(plan.value().instances.data(),
                                        static_cast<std::uint32_t>(plan.value().instances.size())))
        return failure<void>(DiagnosticCode::Failed, "GPU-driven vegetation submission failed");
    return Result<void>::success();
}

struct TerrainVegetationRenderer::State {
    TerrainVegetationRealization      realization;
    ITerrainVegetationGpuResolver*    resolver = nullptr;
    TerrainVegetationGpuFrame         frame{};
    std::unique_ptr<Diagnostic>       gpuError;
    std::unique_ptr<Diagnostic>       shadowError;
    bool                              shadowFailedThisFrame = false;
};

TerrainVegetationRenderer::TerrainVegetationRenderer(TerrainVegetationRealization realization,
                                                     ITerrainVegetationGpuResolver& resolver)
    : state_(std::make_unique<State>()) {
    state_->realization = std::move(realization);
    state_->resolver = &resolver;
    drawerToken_ = graphics::RenderSystem3D::addGpuOpaqueCollector(
        [this](graphics::Graphics&, const graphics::Camera3D::Data&, const glm::mat4&, float,
               std::vector<graphics::GpuInstance>& instances) {
            auto plan = buildTerrainVegetationGpuPlan(state_->realization, *state_->resolver, state_->frame);
            if (plan) {
                state_->gpuError.reset();
                auto& vegetation = plan.value().instances;
                instances.insert(instances.end(), vegetation.begin(), vegetation.end());
            } else {
                state_->gpuError = std::make_unique<Diagnostic>(*plan.error());
            }
            if (!state_->shadowFailedThisFrame) state_->shadowError.reset();
            state_->shadowFailedThisFrame = false;
        });
    shadowDrawerToken_ = graphics::RenderSystem3D::addShadowExtraDrawer(
        [this](graphics::Graphics&, const glm::mat4& lightViewProjection, const graphics::Camera3D::Data&) {
            std::map<std::string_view, const RuntimeInstancePrototype*, std::less<>> metadata;
            for (const auto& prototype : state_->realization.prototypes)
                metadata.emplace(prototype.prototype, &prototype);
            for (const auto& instance : state_->realization.instances) {
                const auto found = metadata.find(instance.prototype);
                const glm::quat rotation(instance.rotation[3], instance.rotation[0], instance.rotation[1],
                                         instance.rotation[2]);
                const glm::mat4 model =
                    glm::translate(glm::mat4(1.f), {instance.position[0], instance.position[1], instance.position[2]}) *
                    glm::mat4_cast(rotation) *
                    glm::scale(glm::mat4(1.f), {instance.scale[0], instance.scale[1], instance.scale[2]});
                std::array<float, 16> modelValues{}, lightValues{};
                std::copy(glm::value_ptr(model), glm::value_ptr(model) + 16, modelValues.begin());
                std::copy(glm::value_ptr(lightViewProjection), glm::value_ptr(lightViewProjection) + 16,
                          lightValues.begin());
                auto drawn = state_->resolver->drawShadow(
                    {instance.prototype, found == metadata.end() ? nullptr : found->second}, modelValues, lightValues);
                if (!drawn) {
                    state_->shadowFailedThisFrame = true;
                    state_->shadowError = std::make_unique<Diagnostic>(*drawn.error());
                    return;
                }
            }
        });
}

TerrainVegetationRenderer::~TerrainVegetationRenderer() {
    graphics::RenderSystem3D::removeShadowExtraDrawer(shadowDrawerToken_);
    graphics::RenderSystem3D::removeGpuOpaqueCollector(drawerToken_);
}

Result<void> TerrainVegetationRenderer::setFrame(TerrainVegetationGpuFrame frame) {
    if (!std::isfinite(frame.timeSeconds) || std::abs(frame.timeSeconds) > 1.0e12)
        return failure<void>(DiagnosticCode::InvalidArgument, "terrain vegetation frame time is invalid");
    state_->frame = frame;
    return Result<void>::success();
}

const Diagnostic* TerrainVegetationRenderer::lastSubmissionError() const {
    return state_->shadowError ? state_->shadowError.get() : state_->gpuError.get();
}

}  // namespace eve::asset_procgen
