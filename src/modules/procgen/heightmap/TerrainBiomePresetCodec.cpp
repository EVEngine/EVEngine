#include "procgen/heightmap/TerrainBiomePreset.h"

#include <limits>

#include "common/Value.h"

namespace eve::procgen {
namespace {
constexpr std::size_t kMaximumPresetJsonBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaximumSpawnerEntries  = 4096;
}  // namespace

Result<std::string> TerrainBiomePreset::snapshotJson() const {
    if (entries_.size() > kMaximumSpawnerEntries)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.biomePreset.snapshot: too many spawner entries"));
    Value::Array entries;
    entries.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (entry.entryId.empty() || entry.entryId.size() > std::numeric_limits<std::uint32_t>::max())
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset.snapshot: invalid entry ID"));
        auto plan = entry.plan.snapshotJson();
        if (!plan.ok()) return Result<std::string>::failure(plan.status());
        entries.emplace_back(Value::Object{{"activeInBiome", entry.activeInBiome},
                                           {"activeInStamper", entry.activeInStamper},
                                           {"autoAssignResources", entry.autoAssignResources},
                                           {"entryId", entry.entryId},
                                           {"plan", std::move(plan.value())}});
    }
    auto encoded = Value(Value::Object{{"entries", std::move(entries)},
                                       {"schema", "eve.procgen.terrain-biome-preset"},
                                       {"version", 1}})
                       .toJson();
    if (!encoded.ok()) return encoded;
    if (encoded.value().size() > kMaximumPresetJsonBytes)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "terrain.biomePreset.snapshot: JSON exceeds size limit"));
    return encoded;
}

Result<void> TerrainBiomePreset::restoreJson(const std::string& json) {
    if (json.size() > kMaximumPresetJsonBytes)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset.restore: JSON exceeds size limit"));
    auto decoded = Value::fromJson(json);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    const auto* root = decoded.value().getIf<Value::Object>();
    if (!root || root->size() != 3 || !root->contains("schema") || !root->contains("version") ||
        !root->contains("entries"))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain.biomePreset.restore: missing or unknown root fields"));
    const auto* schema = root->at("schema").getIf<std::string>();
    const auto* version = root->at("version").getIf<std::int64_t>();
    const auto* entries = root->at("entries").getIf<Value::Array>();
    if (!schema || *schema != "eve.procgen.terrain-biome-preset" || !version ||
        (*version != 0 && *version != 1) || !entries || entries->size() > kMaximumSpawnerEntries)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "terrain.biomePreset.restore: unsupported schema, version or entry count"));

    TerrainBiomePreset candidate;
    for (const auto& encodedEntry : *entries) {
        const auto* entry = encodedEntry.getIf<Value::Object>();
        const std::size_t expectedFields = *version == 0 ? 4 : 5;
        if (!entry || entry->size() != expectedFields || !entry->contains("entryId") ||
            !entry->contains("activeInBiome") || !entry->contains("activeInStamper") ||
            !entry->contains("plan") || (*version >= 1 && !entry->contains("autoAssignResources")))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.biomePreset.restore: missing or unknown entry fields"));
        const auto* entryId = entry->at("entryId").getIf<std::string>();
        const auto* activeInBiome = entry->at("activeInBiome").getIf<bool>();
        const auto* activeInStamper = entry->at("activeInStamper").getIf<bool>();
        const auto* planJson = entry->at("plan").getIf<std::string>();
        const bool* autoAssign = nullptr;
        if (*version >= 1) autoAssign = entry->at("autoAssignResources").getIf<bool>();
        if (!entryId || entryId->empty() || !activeInBiome || !activeInStamper || !planJson ||
            (*version >= 1 && !autoAssign))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "terrain.biomePreset.restore: invalid entry values"));
        TerrainSpawnPlan plan;
        auto restored = plan.restoreJson(*planJson);
        if (!restored.ok()) return Result<void>::failure(restored.status());
        auto added = candidate.addSpawner(*entryId, plan, *activeInBiome, *activeInStamper,
                                           *version == 0 ? true : *autoAssign);
        if (!added.ok()) return Result<void>::failure(added.status());
    }
    entries_.swap(candidate.entries_);
    return Result<void>::success();
}

}  // namespace eve::procgen
