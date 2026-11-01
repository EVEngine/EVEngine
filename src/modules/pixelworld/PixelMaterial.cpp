#include "pixelworld/PixelMaterial.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace eve::pixelworld {
namespace {

eve::Result<MaterialCatalog> invalidCatalog(std::string message, std::string path) {
    return eve::Result<MaterialCatalog>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {}, "pixelworld.catalog"));
}

void hashBytes(std::uint64_t& hash, std::string_view bytes) noexcept {
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
}

template <class T>
void hashValue(std::uint64_t& hash, T value) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        hash ^= std::uint8_t(bits >> (i * 8U));
        hash *= 1099511628211ULL;
    }
}

}  // namespace

eve::Result<MaterialCatalog> MaterialCatalog::create(std::vector<MaterialDefinition> definitions,
                                                     std::vector<MaterialReactionRule> reactions,
                                                     std::vector<MaterialPhaseRule> phaseRules) {
    if (definitions.empty()) return invalidCatalog("material catalog must contain air", "materials");
    if (definitions.size() > std::size_t(std::numeric_limits<std::uint16_t>::max()))
        return invalidCatalog("material catalog exceeds uint16 id capacity", "materials");

    std::unordered_set<std::string> names;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        auto& definition = definitions[index];
        if (std::size_t(definition.id) != index)
            return invalidCatalog("material ids must be contiguous and match canonical order",
                                  "materials[" + std::to_string(index) + "].id");
        if (definition.name.empty() || !names.emplace(definition.name).second)
            return invalidCatalog("material names must be non-empty and unique",
                                  "materials[" + std::to_string(index) + "].name");
        if (definition.heatCapacity == 0)
            return invalidCatalog("material heat capacity must be positive",
                                  "materials[" + std::to_string(index) + "].heatCapacity");
        std::sort(definition.tags.begin(), definition.tags.end());
        if (std::any_of(definition.tags.begin(), definition.tags.end(), [](const std::string& tag) {
                return tag.empty();
            }) || std::adjacent_find(definition.tags.begin(), definition.tags.end()) !=
                       definition.tags.end())
            return invalidCatalog("material tags must be non-empty and unique",
                                  "materials[" + std::to_string(index) + "].tags");
    }
    if (definitions[0].id != MaterialId::Air || definitions[0].state != MaterialState::Empty)
        return invalidCatalog("material zero must be empty air", "materials[0]");

    std::unordered_set<std::string> ruleIds;
    const auto validId = [&definitions](MaterialId id) { return std::size_t(id) < definitions.size(); };
    for (std::size_t index = 0; index < reactions.size(); ++index) {
        const auto& rule = reactions[index];
        if (rule.id.empty() || !ruleIds.emplace(rule.id).second)
            return invalidCatalog("reaction ids must be non-empty and unique",
                                  "reactions[" + std::to_string(index) + "].id");
        if (!validId(rule.first) || !validId(rule.second) || !validId(rule.firstResult) ||
            !validId(rule.secondResult))
            return invalidCatalog("reaction references an unknown material",
                                  "reactions[" + std::to_string(index) + "]");
    }
    std::sort(reactions.begin(), reactions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.priority != rhs.priority ? lhs.priority > rhs.priority : lhs.id < rhs.id;
    });
    std::unordered_set<std::string> phaseIds;
    for (std::size_t index = 0; index < phaseRules.size(); ++index) {
        const auto& rule = phaseRules[index];
        if (rule.id.empty() || !phaseIds.emplace(rule.id).second)
            return invalidCatalog("phase rule ids must be non-empty and unique",
                                  "phaseRules[" + std::to_string(index) + "].id");
        if (!validId(rule.source) || !validId(rule.result))
            return invalidCatalog("phase rule references an unknown material",
                                  "phaseRules[" + std::to_string(index) + "]");
    }
    std::sort(phaseRules.begin(), phaseRules.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.priority != rhs.priority ? lhs.priority > rhs.priority : lhs.id < rhs.id;
    });

    MaterialCatalog catalog;
    catalog.definitions_ = std::move(definitions);
    catalog.reactions_ = std::move(reactions);
    catalog.phaseRules_ = std::move(phaseRules);
    catalog.reactionMask_.assign(catalog.definitions_.size(), 0);
    for (const auto& rule : catalog.reactions_) {
        catalog.reactionMask_[std::size_t(rule.first)] = 1;
        catalog.reactionMask_[std::size_t(rule.second)] = 1;
    }
    catalog.phaseMask_.assign(catalog.definitions_.size(), 0);
    for (const auto& rule : catalog.phaseRules_) catalog.phaseMask_[std::size_t(rule.source)] = 1;
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& definition : catalog.definitions_) {
        hashValue(hash, std::uint16_t(definition.id));
        hashBytes(hash, definition.name);
        hashValue(hash, std::uint8_t(definition.state));
        hashValue(hash, definition.density);
        hashValue(hash, definition.viscosity);
        hashValue(hash, definition.thermalConductivity);
        hashValue(hash, definition.heatCapacity);
        hashValue(hash, definition.defaultTemperature);
        hashValue(hash, definition.ignitionTemperature);
        hashValue(hash, definition.meltingTemperature);
        hashValue(hash, definition.boilingTemperature);
        hashValue(hash, definition.defaultLifetime);
        hashValue(hash, std::uint8_t(definition.flammable));
        hashValue(hash, definition.blastResistance);
        hashValue(hash, definition.displayRgba);
        hashValue(hash, std::uint32_t(definition.tags.size()));
        for (const auto& tag : definition.tags) hashBytes(hash, tag);
    }
    for (const auto& rule : catalog.reactions_) {
        hashBytes(hash, rule.id);
        hashValue(hash, std::uint16_t(rule.first));
        hashValue(hash, std::uint16_t(rule.second));
        hashValue(hash, std::uint16_t(rule.firstResult));
        hashValue(hash, std::uint16_t(rule.secondResult));
        hashValue(hash, rule.minimumTemperature);
        hashValue(hash, rule.heatDelta);
        hashValue(hash, rule.priority);
    }
    for (const auto& rule : catalog.phaseRules_) {
        hashBytes(hash, rule.id);
        hashValue(hash, std::uint16_t(rule.source));
        hashValue(hash, std::uint16_t(rule.result));
        hashValue(hash, std::uint8_t(rule.direction));
        hashValue(hash, rule.threshold);
        hashValue(hash, rule.temperatureDelta);
        hashValue(hash, rule.priority);
    }
    catalog.fingerprint_ = hash;
    return eve::Result<MaterialCatalog>::success(std::move(catalog));
}

MaterialCatalog MaterialCatalog::builtIn() {
    std::vector<MaterialDefinition> materials{
        {MaterialId::Air, "air", MaterialState::Empty, 0, 0, 8, 1},
        {MaterialId::Stone, "stone", MaterialState::Solid, 255, 255, 30, 180},
        {MaterialId::Sand, "sand", MaterialState::Powder, 180, 35, 18, 80},
        {MaterialId::Water, "water", MaterialState::Liquid, 100, 24, 45, 120, 32767, 0, 100},
        {MaterialId::Oil, "oil", MaterialState::Liquid, 80, 70, 12, 90, 180, 32767, 280, 0, true},
        {MaterialId::Fire, "fire", MaterialState::Energy, 1, 0, 80, 1, 32767, 32767, 32767, 30,
         false, 700},
        {MaterialId::Steam, "steam", MaterialState::Gas, 2, 5, 22, 30, 32767, 32767, 32767, 180,
         false, 110},
        {MaterialId::Wood, "wood", MaterialState::Solid, 220, 255, 8, 140, 240, 32767, 32767, 0, true},
        {MaterialId::Ice, "ice", MaterialState::Solid, 105, 255, 35, 110, 32767, 0, 32767, 0, false, -10},
        {MaterialId::Lava, "lava", MaterialState::Liquid, 210, 180, 55, 200, 32767, 900, 32767, 0, false, 1300},
        {MaterialId::Smoke, "smoke", MaterialState::Gas, 3, 8, 12, 20, 32767, 32767, 32767, 220},
        {MaterialId::Gunpowder, "gunpowder", MaterialState::Powder, 165, 40, 20, 70, 160, 32767, 32767, 0, true},
        {MaterialId::Acid, "acid", MaterialState::Liquid, 110, 45, 32, 100, 32767, 32767, 118},
    };
    std::vector<MaterialReactionRule> reactions{
        {"fire.water.boil", MaterialId::Fire, MaterialId::Water, MaterialId::Fire, MaterialId::Steam,
         -32768, 40, 200},
        {"fire.oil.ignite", MaterialId::Fire, MaterialId::Oil, MaterialId::Fire, MaterialId::Fire,
         -32768, 80, 150},
        {"fire.wood.ignite", MaterialId::Fire, MaterialId::Wood, MaterialId::Fire, MaterialId::Fire,
         -32768, 60, 140},
        {"fire.gunpowder.ignite", MaterialId::Fire, MaterialId::Gunpowder, MaterialId::Fire,
         MaterialId::Fire, -32768, 300, 180},
        {"acid.stone.dissolve", MaterialId::Acid, MaterialId::Stone, MaterialId::Water,
         MaterialId::Sand, -32768, 0, 120},
        {"lava.water.quench", MaterialId::Lava, MaterialId::Water, MaterialId::Stone,
         MaterialId::Steam, -32768, 80, 220},
    };
    materials[std::size_t(MaterialId::Air)].blastResistance = 0;
    materials[std::size_t(MaterialId::Stone)].blastResistance = 240;
    materials[std::size_t(MaterialId::Sand)].blastResistance = 8;
    materials[std::size_t(MaterialId::Water)].blastResistance = 0;
    materials[std::size_t(MaterialId::Oil)].blastResistance = 0;
    materials[std::size_t(MaterialId::Fire)].blastResistance = 0;
    materials[std::size_t(MaterialId::Steam)].blastResistance = 0;
    materials[std::size_t(MaterialId::Wood)].blastResistance = 45;
    materials[std::size_t(MaterialId::Ice)].blastResistance = 70;
    materials[std::size_t(MaterialId::Lava)].blastResistance = 0;
    materials[std::size_t(MaterialId::Smoke)].blastResistance = 0;
    materials[std::size_t(MaterialId::Gunpowder)].blastResistance = 5;
    materials[std::size_t(MaterialId::Acid)].blastResistance = 0;
    const auto decorate = [&materials](MaterialId id, std::uint32_t color,
                                       std::vector<std::string> tags) {
        auto& material = materials[std::size_t(id)];
        material.displayRgba = color;
        material.tags = std::move(tags);
    };
    decorate(MaterialId::Air, 0x00000000U, {"empty"});
    decorate(MaterialId::Stone, 0x4D5261FFU, {"terrain", "solid"});
    decorate(MaterialId::Sand, 0xE0B04DFFU, {"granular", "mobile"});
    decorate(MaterialId::Water, 0x1F61F2DBU, {"liquid", "mobile"});
    decorate(MaterialId::Oil, 0x423314FFU, {"flammable", "liquid", "mobile"});
    decorate(MaterialId::Fire, 0xFF3D08FFU, {"energy", "hot", "mobile"});
    decorate(MaterialId::Steam, 0xB8CCE08CU, {"gas", "mobile"});
    decorate(MaterialId::Wood, 0x6B3312FFU, {"flammable", "solid", "terrain"});
    decorate(MaterialId::Ice, 0xADDBFFF2U, {"cold", "solid", "terrain"});
    decorate(MaterialId::Lava, 0xFF2E03FFU, {"hot", "liquid", "mobile"});
    decorate(MaterialId::Smoke, 0x4D4A5294U, {"gas", "mobile"});
    decorate(MaterialId::Gunpowder, 0x3D3833FFU, {"explosive", "granular", "mobile"});
    decorate(MaterialId::Acid, 0x4DEB3DE0U, {"corrosive", "liquid", "mobile"});
    std::vector<MaterialPhaseRule> phaseRules{
        {"water.freeze", MaterialId::Water, MaterialId::Ice, TemperatureDirection::AtOrBelow, 0, 0, 100},
        {"water.boil", MaterialId::Water, MaterialId::Steam, TemperatureDirection::AtOrAbove, 100, -20, 100},
        {"ice.melt", MaterialId::Ice, MaterialId::Water, TemperatureDirection::AtOrAbove, 1, 0, 100},
        {"stone.melt", MaterialId::Stone, MaterialId::Lava, TemperatureDirection::AtOrAbove, 1200, -200, 100},
        {"lava.solidify", MaterialId::Lava, MaterialId::Stone, TemperatureDirection::AtOrBelow, 800, 100, 100},
    };
    return std::move(MaterialCatalog::create(std::move(materials), std::move(reactions), std::move(phaseRules)))
        .expect("built-in PixelWorld material catalog must be valid");
}

eve::Result<MaterialId> MaterialCatalog::resolve(std::string_view name) const {
    for (const auto& definition : definitions_)
        if (definition.name == name) return eve::Result<MaterialId>::success(definition.id);
    return eve::Result<MaterialId>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::NotFound, "unknown pixel material: " + std::string(name), "material", {},
        "pixelworld.catalog"));
}

const MaterialDefinition& MaterialCatalog::definition(MaterialId id) const noexcept {
    const std::size_t index = std::size_t(id);
    return definitions_[index < definitions_.size() ? index : 0];
}

std::span<const MaterialDefinition> MaterialCatalog::definitions() const noexcept { return definitions_; }
std::span<const MaterialReactionRule> MaterialCatalog::reactions() const noexcept { return reactions_; }
std::span<const MaterialPhaseRule> MaterialCatalog::phaseRules() const noexcept { return phaseRules_; }
bool MaterialCatalog::canReact(MaterialId id) const noexcept {
    const std::size_t index = std::size_t(id);
    return index < reactionMask_.size() && reactionMask_[index] != 0;
}
bool MaterialCatalog::canPhaseTransition(MaterialId id) const noexcept {
    const std::size_t index = std::size_t(id);
    return index < phaseMask_.size() && phaseMask_[index] != 0;
}
std::uint64_t MaterialCatalog::fingerprint() const noexcept { return fingerprint_; }

}  // namespace eve::pixelworld
