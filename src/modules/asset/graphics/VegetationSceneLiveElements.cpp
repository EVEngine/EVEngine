#include "asset/graphics/VegetationSceneLiveElements.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <set>
#include <string_view>

namespace eve::asset_graphics {
namespace {
bool finite(float value) { return std::isfinite(value); }

bool validGuid(std::string_view value, bool allowEmpty) {
    return (allowEmpty && value.empty()) ||
           (value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return std::isxdigit(character) != 0;
            }));
}

bool validElement(const VegetationSceneElement& element) {
    static const std::set<std::string> kinds{
        "color-effect", "color-map", "color-noise", "color-tint", "extras-alpha", "extras-emissive",
        "extras-overlay", "extras-wetness", "motion-advanced", "motion-interaction", "motion-wind-power",
        "vertex-conform-model", "vertex-conform-simple", "vertex-conform-terrain", "vertex-height-offset",
        "vertex-height", "vertex-orientation-model", "vertex-orientation-terrain", "vertex-size"};
    const std::uint32_t expectedChannel = element.kind.starts_with("color-") ? 0u
                                           : element.kind.starts_with("extras-") ? 1u
                                           : element.kind.starts_with("motion-") ? 2u
                                                                                 : 3u;
    const float rotationLength = std::sqrt(std::inner_product(element.rotation.begin(), element.rotation.end(),
                                                               element.rotation.begin(), 0.f));
    std::set<std::string> propertyNames;
    const auto finiteValues = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return finite(value); });
    };
    if (element.sourceFileId == 0 || !validGuid(element.shaderGuid, false) ||
        !validGuid(element.textureGuid, true) || !kinds.contains(element.kind) || element.channel != expectedChannel ||
        (element.visibility != -1 && element.visibility != 0 && element.visibility != 10 &&
         element.visibility != 20) ||
        element.layers == 0 || (element.layers & ~0x1ffu) || !finite(element.intensity) || element.intensity < 0.f ||
        element.intensity > 1.f || !finite(element.motionPower) || element.motionPower < 0.f ||
        element.motionPower > 1.f || !finiteValues(element.position) || !finiteValues(element.rotation) ||
        !finiteValues(element.scale) || !finiteValues(element.value) || !finiteValues(element.remap) ||
        !finiteValues(element.seasons[0]) || !finiteValues(element.seasons[1]) ||
        !finiteValues(element.seasons[2]) || !finiteValues(element.seasons[3]) ||
        std::abs(rotationLength - 1.f) > 1e-3f ||
        std::any_of(element.scale.begin(), element.scale.end(), [](float value) { return value <= 0.f; }) ||
        element.remap[0] > element.remap[1] || element.remap[2] > element.remap[3] ||
        element.remap[4] > element.remap[5] || element.blendRgb < 0 || element.blendRgb > 2 ||
        element.blendAlpha < 0 || element.blendAlpha > 1 ||
        (element.directionMode != 10 && element.directionMode != 20 && element.directionMode != 30 &&
         element.directionMode != 40) ||
        (element.motionMode != 13 && element.motionMode != 15) || element.properties.size() > 256 ||
        (!element.textureAsset.empty() && !AssetRef::parse(element.textureAsset)))
        return false;
    for (const auto& property : element.properties) {
        if (property.name.empty() || property.name.size() > 128 || property.type < 0 || property.type > 2 ||
            !validGuid(property.textureGuid, true) ||
            !finiteValues(property.vector) || !finite(property.value) ||
            (!property.textureAsset.empty() && !AssetRef::parse(property.textureAsset)) ||
            !propertyNames.emplace(property.name).second)
            return false;
    }
    return true;
}
}  // namespace

Result<std::unique_ptr<VegetationSceneLiveElements>> VegetationSceneLiveElements::create(
    LoadedVegetationScene baseScene, std::uint64_t owner) {
    if (owner == 0 || baseScene.elements.size() > 4096)
        return Result<std::unique_ptr<VegetationSceneLiveElements>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "vegetation live-element registry identity or scene is invalid", {}, {},
            "asset.graphics.vegetation-live-elements"));
    if (std::any_of(baseScene.elements.begin(), baseScene.elements.end(), [](const auto& element) {
            return !validElement(element);
        }))
        return Result<std::unique_ptr<VegetationSceneLiveElements>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation live-element base scene is invalid", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    try {
        return Result<std::unique_ptr<VegetationSceneLiveElements>>::success(
            std::unique_ptr<VegetationSceneLiveElements>(new VegetationSceneLiveElements(std::move(baseScene), owner)));
    } catch (const std::bad_alloc&) {
        return Result<std::unique_ptr<VegetationSceneLiveElements>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation live-element registry allocation failed", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    }
}

Result<LoadedVegetationScene> VegetationSceneLiveElements::snapshot() const {
    try {
        LoadedVegetationScene result = baseScene_;
        result.elements.reserve(baseScene_.elements.size() + slots_.size());
        for (const auto& slot : slots_)
            if (slot.element) result.elements.push_back(*slot.element);
        return Result<LoadedVegetationScene>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<LoadedVegetationScene>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation live-element snapshot allocation failed", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    }
}

Result<VegetationSceneElementPublication> VegetationSceneLiveElements::registerElement(
    std::uint64_t expectedRevision, std::uint64_t expectedGpuRevision, const VegetationSceneElement& element,
    VegetationSceneGpuRuntime& runtime, const graphics::PbrSurface& baseSurface,
    const graphics::VegetationMotion& baseMotion, const std::map<std::string, graphics::VegetationMask>& masks,
    const VegetationSceneGpuBuild& build) {
    if (expectedRevision != revision_ || expectedGpuRevision != runtime.sourceRevision())
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "vegetation live-element state changed before registration", {},
                              {}, "asset.graphics.vegetation-live-elements"));
    if (revision_ == std::numeric_limits<std::uint64_t>::max() || !validElement(element))
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation live Element is invalid", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    std::size_t slotIndex = 0;
    while (slotIndex < slots_.size() && (slots_[slotIndex].element || slots_[slotIndex].retired)) ++slotIndex;
    const auto activeCount = std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
        return slot.element.has_value();
    });
    if (baseScene_.elements.size() + std::size_t(activeCount) + 1 > 4096)
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation live-element capacity exceeded", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    auto candidate = snapshot();
    if (!candidate) return Result<VegetationSceneElementPublication>::failure(candidate.status());
    try {
        candidate.value().elements.push_back(element);
        if (slotIndex == slots_.size()) slots_.reserve(slots_.size() + 1);
    } catch (const std::bad_alloc&) {
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation live-element registration allocation failed", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    }
    auto published = runtime.replace(expectedGpuRevision, baseSurface, baseMotion, candidate.value(), masks, build);
    if (!published) return Result<VegetationSceneElementPublication>::failure(published.status());
    if (slotIndex == slots_.size()) slots_.push_back({});
    auto& slot = slots_[slotIndex];
    slot.element.emplace(std::move(candidate.value().elements.back()));
    ++revision_;
    return Result<VegetationSceneElementPublication>::success(
        {{owner_, static_cast<VegetationSceneElementHandle::index_type>(slotIndex), slot.generation}, revision_,
         published.value()});
}

Result<VegetationSceneElementPublication> VegetationSceneLiveElements::withdrawElement(
    std::uint64_t expectedRevision, std::uint64_t expectedGpuRevision, VegetationSceneElementHandle handle,
    VegetationSceneGpuRuntime& runtime, const graphics::PbrSurface& baseSurface,
    const graphics::VegetationMotion& baseMotion, const std::map<std::string, graphics::VegetationMask>& masks,
    const VegetationSceneGpuBuild& build) {
    if (expectedRevision != revision_ || expectedGpuRevision != runtime.sourceRevision())
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "vegetation live-element state changed before withdrawal", {},
                              {}, "asset.graphics.vegetation-live-elements"));
    if (!handle.isValid() || handle.owner() != owner_ || handle.index() >= slots_.size())
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "vegetation live Element handle is foreign or missing", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    auto& slot = slots_[handle.index()];
    if (!slot.element || slot.generation != handle.generation())
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "vegetation live Element handle is stale or withdrawn", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        return Result<VegetationSceneElementPublication>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation live-element revision is exhausted", {}, {},
                              "asset.graphics.vegetation-live-elements"));
    auto candidate = snapshot();
    if (!candidate) return Result<VegetationSceneElementPublication>::failure(candidate.status());
    const std::size_t dynamicOffset = baseScene_.elements.size();
    std::size_t activeBefore = 0;
    for (std::size_t index = 0; index < handle.index(); ++index)
        if (slots_[index].element) ++activeBefore;
    candidate.value().elements.erase(candidate.value().elements.begin() + dynamicOffset + activeBefore);
    auto published = runtime.replace(expectedGpuRevision, baseSurface, baseMotion, candidate.value(), masks, build);
    if (!published) return Result<VegetationSceneElementPublication>::failure(published.status());
    slot.element.reset();
    const auto next = VegetationSceneElementHandle::LocalHandle::nextGeneration(slot.generation);
    if (next)
        slot.generation = *next;
    else
        slot.retired = true;
    ++revision_;
    return Result<VegetationSceneElementPublication>::success({handle, revision_, published.value()});
}

}  // namespace eve::asset_graphics
