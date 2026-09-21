#include "asset/graphics/VegetationSceneInstance.h"

#include <algorithm>
#include <cctype>
#include <new>

namespace eve::asset_graphics {
namespace {
std::string folded(std::string value) {
    for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}
}  // namespace

Result<VegetationSceneAssociation> VegetationSceneAssociationLoader::load(
    const AssetRef& sceneTemplate, const asset::EvpackCapabilities& capabilities) const {
    auto scene = asset_scene::EvpackSceneTemplateLoader(reader_).load(sceneTemplate, capabilities);
    if (!scene) return Result<VegetationSceneAssociation>::failure(scene.status());
    if (scene.value().sourceGuid.empty())
        return Result<VegetationSceneAssociation>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "legacy Scene Template has no source GUID for manager association",
            sceneTemplate.format(), {}, "asset.graphics.vegetation-scene-instance"));
    auto candidates = reader_.listAssets("eve.vegetation-scene/1", capabilities, 4096);
    if (!candidates) return Result<VegetationSceneAssociation>::failure(candidates.status());
    std::optional<LoadedVegetationScene> match;
    const auto expected = folded(scene.value().sourceGuid);
    for (const auto& candidate : candidates.value()) {
        auto manager = EvpackVegetationSceneLoader(reader_).load(candidate, capabilities);
        if (!manager) return Result<VegetationSceneAssociation>::failure(manager.status());
        if (folded(manager.value().sourceGuid) != expected) continue;
        if (match)
            return Result<VegetationSceneAssociation>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Scene Template matches multiple vegetation managers",
                                  sceneTemplate.format(), {}, "asset.graphics.vegetation-scene-instance"));
        match.emplace(std::move(manager).takeValue());
    }
    if (!match)
        return Result<VegetationSceneAssociation>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Scene Template has no vegetation manager",
                              sceneTemplate.format(), {}, "asset.graphics.vegetation-scene-instance"));
    auto masks = loadVegetationSceneElementMasks(reader_, *match, capabilities);
    if (!masks) return Result<VegetationSceneAssociation>::failure(masks.status());
    return Result<VegetationSceneAssociation>::success(
        {std::move(scene).takeValue(), std::move(*match), std::move(masks).takeValue()});
}

Result<std::unique_ptr<VegetationSceneInstance>> VegetationSceneInstance::create(
    const asset::EvpackResourceReader& reader, graphics::IResourceFactory& factory,
    const AssetRef& sceneTemplate, const asset::EvpackCapabilities& capabilities,
    graphics::PbrSurface baseSurface, graphics::VegetationMotion baseMotion,
    const VegetationSceneGpuBuild& build, std::uint64_t owner) {
    auto association = VegetationSceneAssociationLoader(reader).load(sceneTemplate, capabilities);
    if (!association) return Result<std::unique_ptr<VegetationSceneInstance>>::failure(association.status());
    auto runtime = VegetationSceneGpuRuntime::create(factory, baseSurface, baseMotion, association.value().manager,
                                                     association.value().masks, build);
    if (!runtime) return Result<std::unique_ptr<VegetationSceneInstance>>::failure(runtime.status());
    auto live = VegetationSceneLiveElements::create(association.value().manager, owner);
    if (!live) return Result<std::unique_ptr<VegetationSceneInstance>>::failure(live.status());
    try {
        auto result = std::unique_ptr<VegetationSceneInstance>(new VegetationSceneInstance(
            std::move(association).takeValue(), std::move(baseSurface), std::move(baseMotion),
            std::move(runtime).takeValue(), std::move(live).takeValue()));
        return Result<std::unique_ptr<VegetationSceneInstance>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::unique_ptr<VegetationSceneInstance>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation scene instance allocation failed", {}, {},
                              "asset.graphics.vegetation-scene-instance"));
    }
}

Result<VegetationSceneElementPublication> VegetationSceneInstance::registerElement(
    std::uint64_t expectedRevision, const VegetationSceneElement& element,
    const VegetationSceneGpuBuild& build) {
    return liveElements_->registerElement(expectedRevision, runtime_->sourceRevision(), element, *runtime_,
                                          baseSurface_, baseMotion_, association_.masks, build);
}

Result<VegetationSceneElementPublication> VegetationSceneInstance::withdrawElement(
    std::uint64_t expectedRevision, VegetationSceneElementHandle handle, const VegetationSceneGpuBuild& build) {
    return liveElements_->withdrawElement(expectedRevision, runtime_->sourceRevision(), handle, *runtime_,
                                          baseSurface_, baseMotion_, association_.masks, build);
}

}  // namespace eve::asset_graphics
