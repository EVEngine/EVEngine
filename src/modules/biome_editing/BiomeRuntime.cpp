#include "biome_editing/BiomeTarget.h"
#include "procgen/Biome.h"
#include "procgen/SpatialData.h"

#include <cmath>
#include <utility>

namespace eve::biome_editing {
namespace {
template <class T>
EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}
}  // namespace

BiomeDocumentRuntime::BiomeDocumentRuntime()  = default;
BiomeDocumentRuntime::~BiomeDocumentRuntime() = default;

EditorResult<void> BiomeDocumentRuntime::publish(const BiomeDocumentTarget& document,
                                                 const IBiomeSpatialResolver& resolver) {
    const auto diagnostics = document.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));

    auto candidate = std::make_unique<procgen::BiomeRules>();
    for (const auto& layer : document.layers()) {
        auto spatial = resolver.resolve(layer.spatialAsset);
        if (!spatial.ok()) return EditorResult<void>::failure(spatial.status());
        if (!spatial.value())
            return fail<void>(EditorStatus::NotFound, "editor.biome.runtime-spatial",
                              "Biome spatial resolver returned no spatial data");
        if (!candidate->addLayer(layer.name, spatial.value(), layer.priority, layer.density))
            return fail<void>(EditorStatus::Failed, "editor.biome.runtime-layer",
                              "Biome runtime rejected a validated layer");
        for (const auto& asset : layer.assets)
            if (!candidate->addAsset(layer.name, asset.asset, asset.weight, asset.minScale, asset.maxScale,
                                     asset.randomYaw))
                return fail<void>(EditorStatus::Failed, "editor.biome.runtime-asset",
                                  "Biome runtime rejected a validated asset");
    }
    for (const auto& reference : document.exclusions()) {
        auto spatial = resolver.resolve(reference);
        if (!spatial.ok()) return EditorResult<void>::failure(spatial.status());
        if (!spatial.value())
            return fail<void>(EditorStatus::NotFound, "editor.biome.runtime-exclusion-spatial",
                              "Biome exclusion resolver returned no spatial data");
        if (!candidate->addExclusion(spatial.value()))
            return fail<void>(EditorStatus::Failed, "editor.biome.runtime-exclusion",
                              "Biome runtime rejected a validated exclusion");
    }
    rules_    = std::move(candidate);
    revision_ = document.revision();
    return eve::editing::applied<void>(diagnostics);
}

EditorResult<std::unique_ptr<procgen::PointSet>> BiomeDocumentRuntime::preview(
    procgen::SpatialData* domain, float spacing, std::uint32_t seed, float jitter, Revision expectedRevision) {
    if (expectedRevision != revision_)
        return fail<std::unique_ptr<procgen::PointSet>>(EditorStatus::Conflict, "editor.biome.stale",
                                                        "Biome preview generation is stale");
    if (!rules_ || !domain || !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(jitter) || jitter < 0 ||
        jitter > 1)
        return fail<std::unique_ptr<procgen::PointSet>>(EditorStatus::Rejected, "editor.biome.preview",
                                                        "Biome preview inputs are invalid");
    std::unique_ptr<procgen::PointSet> points(rules_->generate(domain, spacing, seed, jitter));
    if (!points)
        return fail<std::unique_ptr<procgen::PointSet>>(EditorStatus::Failed, "editor.biome.generate",
                                                        rules_->getError());
    return eve::editing::applied<std::unique_ptr<procgen::PointSet>>(std::move(points));
}
}  // namespace eve::biome_editing
