#include "procgen/biome/editing/BiomeTarget.h"
#include "procgen/Biome.h"
#include "procgen/SpatialData.h"

#include <cmath>
#include <limits>
#include <utility>

namespace eve::biome_editing {
struct BiomePublication {
    TargetId     biome;
    Revision     revision = 0;
    BiomeSchemes schemes;
};
namespace {
template <class T>
Result<T> regionError(EditorStatus status, const char* message) {
    return eve::editing::failed<T>(status, RuleId("biome.region.binding"), message);
}
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}
const std::string* text(const EditorValue& value, const char* key) {
    const auto* entry = field(value, key);
    return entry ? entry->getIf<std::string>() : nullptr;
}
}  // namespace

BiomeRegionBinding::BiomeRegionBinding(ObjectId region, eve::ResourceUri spatial, std::uint32_t seed,
                                       std::weak_ptr<BiomePublication> publication)
    : region_(std::move(region)), spatial_(std::move(spatial)), seed_(seed), publication_(std::move(publication)) {}

Result<BiomeRegionConfiguration> BiomeRegionBinding::configuration() const {
    const auto published = publication_.lock();
    if (!published) return regionError<BiomeRegionConfiguration>(EditorStatus::NotFound, "Biome runtime has expired");
    return eve::editing::applied<BiomeRegionConfiguration>(
        {region_, published->biome, spatial_, seed_, published->revision, published->schemes});
}

Result<EditorValue> BiomeRegionBinding::snapshotValue() const {
    const auto published = publication_.lock();
    if (!published) return regionError<EditorValue>(EditorStatus::NotFound, "Biome runtime has expired");
    return eve::editing::applied<EditorValue>(EditorValue::Object{{"schema", "eve.biome.region-binding"},
                                                                  {"schemaVersion", std::int64_t{1}},
                                                                  {"region", region_.value()},
                                                                  {"biome", published->biome.value()},
                                                                  {"spatial", spatial_.format()},
                                                                  {"seed", static_cast<std::int64_t>(seed_)}});
}

BiomeDocumentRuntime::BiomeDocumentRuntime()  = default;
BiomeDocumentRuntime::~BiomeDocumentRuntime() = default;

Result<void> BiomeDocumentRuntime::publish(const BiomeDocumentTarget&   document,
                                                 const IBiomeSpatialResolver& resolver) {
    if (document.targetId().empty())
        return regionError<void>(EditorStatus::Rejected, "Biome publication requires a stable document identity");
    if (publication_ && publication_->biome != document.targetId())
        return regionError<void>(EditorStatus::Conflict,
                                 "Runtime cannot change biome identity while regions follow it");
    const auto diagnostics = document.validate();
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return Result<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));

    auto candidate = std::make_unique<procgen::BiomeRules>();
    for (const auto& layer : document.layers()) {
        auto spatial = resolver.resolve(layer.spatialAsset);
        if (!spatial.ok()) return Result<void>::failure(spatial.status());
        if (!spatial.value())
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.biome.runtime-spatial"),
                                              "Biome spatial resolver returned no spatial data");
        if (!candidate->addLayer(layer.name, spatial.value(), layer.priority, layer.density))
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.biome.runtime-layer"),
                                              "Biome runtime rejected a validated layer");
        for (const auto& asset : layer.assets)
            if (!candidate->addAsset(layer.name, asset.asset, asset.weight, asset.minScale, asset.maxScale,
                                     asset.randomYaw))
                return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.biome.runtime-asset"),
                                                  "Biome runtime rejected a validated asset");
    }
    for (const auto& reference : document.exclusions()) {
        auto spatial = resolver.resolve(reference);
        if (!spatial.ok()) return Result<void>::failure(spatial.status());
        if (!spatial.value())
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.biome.runtime-exclusion-spatial"),
                                              "Biome exclusion resolver returned no spatial data");
        if (!candidate->addExclusion(spatial.value()))
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.biome.runtime-exclusion"),
                                              "Biome runtime rejected a validated exclusion");
    }
    // Allocate/copy before replacing either projection. Owner-thread publication invokes no callbacks.
    auto published = BiomePublication{document.targetId(), document.revision(), document.schemes()};
    auto slot      = publication_ ? publication_ : std::make_shared<BiomePublication>();
    rules_    = std::move(candidate);
    revision_ = document.revision();
    *slot          = std::move(published);
    publication_   = std::move(slot);
    return eve::editing::applied<void>(diagnostics);
}

Result<BiomeRegionBinding> BiomeDocumentRuntime::bindRegion(ObjectId region, std::string_view spatial,
                                                            std::uint32_t seed) const {
    if (!publication_)
        return regionError<BiomeRegionBinding>(EditorStatus::NotFound, "Publish the biome before binding a region");
    if (region.empty() || region.value().size() > 4096 || spatial.size() > 4096)
        return regionError<BiomeRegionBinding>(EditorStatus::Rejected, "Region identity or spatial URI exceeds bounds");
    auto uri = eve::ResourceUri::parse(spatial);
    if (!uri.ok()) return Result<BiomeRegionBinding>::failure(uri.status());
    return eve::editing::applied<BiomeRegionBinding>(
        BiomeRegionBinding(std::move(region), std::move(uri).takeValue(), seed, publication_));
}

Result<BiomeRegionBinding> BiomeDocumentRuntime::restoreRegion(const EditorValue& snapshot) const {
    if (!snapshot.isWithinLimits(4, 32, 32768))
        return regionError<BiomeRegionBinding>(EditorStatus::Rejected, "Region binding snapshot exceeds limits");
    const auto* object       = snapshot.getIf<EditorValue::Object>();
    const auto* schema       = text(snapshot, "schema");
    const auto* versionField = field(snapshot, "schemaVersion");
    const auto* version      = versionField ? versionField->getIf<std::int64_t>() : nullptr;
    const auto* region       = text(snapshot, "region");
    const auto* biome        = text(snapshot, "biome");
    const auto* spatial      = text(snapshot, "spatial");
    const auto* seedField    = field(snapshot, "seed");
    const auto* seed         = seedField ? seedField->getIf<std::int64_t>() : nullptr;
    if (!object || object->size() != 6 || !schema || *schema != "eve.biome.region-binding" || !version ||
        *version != 1 || !region || !biome || !spatial || !seed || *seed < 0 ||
        *seed > std::numeric_limits<std::uint32_t>::max())
        return regionError<BiomeRegionBinding>(EditorStatus::Rejected, "Invalid region binding snapshot");
    if (!publication_ || publication_->biome != TargetId(*biome))
        return regionError<BiomeRegionBinding>(EditorStatus::Conflict,
                                               "Region snapshot names a different or unpublished biome");
    return bindRegion(ObjectId(*region), *spatial, static_cast<std::uint32_t>(*seed));
}

Result<std::unique_ptr<procgen::PointSet>> BiomeDocumentRuntime::previewRegion(const BiomeRegionBinding&    binding,
                                                                               const IBiomeSpatialResolver& resolver,
                                                                               float spacing, float jitter) {
    const auto published = binding.publication_.lock();
    if (!published || published != publication_)
        return regionError<std::unique_ptr<procgen::PointSet>>(EditorStatus::Conflict,
                                                               "Region binding is stale or belongs to another runtime");
    auto spatial = resolver.resolve(binding.spatial_.format());
    if (!spatial.ok()) return Result<std::unique_ptr<procgen::PointSet>>::failure(spatial.status());
    return preview(spatial.value(), spacing, binding.seed_, jitter, revision_);
}

Result<std::unique_ptr<procgen::PointSet>> BiomeDocumentRuntime::preview(procgen::SpatialData* domain,
                                                                               float spacing, std::uint32_t seed,
                                                                               float    jitter,
                                                                               Revision expectedRevision) {
    if (expectedRevision != revision_)
        return eve::editing::failed<std::unique_ptr<procgen::PointSet>>(
            EditorStatus::Conflict, RuleId("editor.biome.stale"), "Biome preview generation is stale");
    if (!rules_ || !domain || !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(jitter) || jitter < 0 ||
        jitter > 1)
        return eve::editing::failed<std::unique_ptr<procgen::PointSet>>(
            EditorStatus::Rejected, RuleId("editor.biome.preview"), "Biome preview inputs are invalid");
    std::unique_ptr<procgen::PointSet> points(rules_->generate(domain, spacing, seed, jitter));
    if (!points)
        return eve::editing::failed<std::unique_ptr<procgen::PointSet>>(
            EditorStatus::Failed, RuleId("editor.biome.generate"), rules_->getError());
    return eve::editing::applied<std::unique_ptr<procgen::PointSet>>(std::move(points));
}
}  // namespace eve::biome_editing
