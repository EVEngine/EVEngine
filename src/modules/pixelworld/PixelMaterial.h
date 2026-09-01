#pragma once

#include "common/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::pixelworld {

/** @brief Compact stable material identifier stored in each authoritative cell. */
enum class MaterialId : std::uint16_t {
    Air = 0,
    Stone,
    Sand,
    Water,
    Oil,
    Fire,
    Steam,
    Wood,
    Ice,
    Lava,
    Smoke,
    Gunpowder,
    Acid,
};

/** @brief Broad movement class for one pixel material. */
enum class MaterialState : std::uint8_t { Empty, Solid, Powder, Liquid, Gas, Energy };

/** @brief Immutable validated properties for one catalog material. */
struct MaterialDefinition {
    MaterialId id = MaterialId::Air;
    std::string name = "air";
    MaterialState state = MaterialState::Empty;
    std::uint16_t density = 0;
    std::uint8_t viscosity = 0;
    std::uint8_t thermalConductivity = 0;
    std::uint16_t heatCapacity = 1;
    std::int16_t ignitionTemperature = 32767;
    std::int16_t meltingTemperature = 32767;
    std::int16_t boilingTemperature = 32767;
    std::uint16_t defaultLifetime = 0;
    bool flammable = false;
    std::int16_t defaultTemperature = 20;
    std::uint16_t blastResistance = 100;
    /** @brief Canonical preview/render color encoded as 0xRRGGBBAA. */
    std::uint32_t displayRgba = 0xFF00FFFFU;
    /** @brief Canonical sorted semantic tags used by authoring and rule queries. */
    std::vector<std::string> tags;
};

/** @brief Comparison direction for a deterministic temperature transition. */
enum class TemperatureDirection : std::uint8_t { AtOrBelow, AtOrAbove };

/** @brief Data-driven phase transition evaluated after heat diffusion. */
struct MaterialPhaseRule {
    std::string id;
    MaterialId source = MaterialId::Air;
    MaterialId result = MaterialId::Air;
    TemperatureDirection direction = TemperatureDirection::AtOrAbove;
    std::int16_t threshold = 0;
    std::int16_t temperatureDelta = 0;
    std::int32_t priority = 0;
};

/** @brief Deterministically ordered binary material reaction. */
struct MaterialReactionRule {
    std::string id;
    MaterialId first = MaterialId::Air;
    MaterialId second = MaterialId::Air;
    MaterialId firstResult = MaterialId::Air;
    MaterialId secondResult = MaterialId::Air;
    std::int16_t minimumTemperature = -32768;
    std::int16_t heatDelta = 0;
    std::int32_t priority = 0;
};

/** @brief Owning immutable material and reaction table used by one PixelWorld. */
class MaterialCatalog {
public:
    /** @brief Validate and own a complete catalog candidate transactionally. */
    [[nodiscard]] static eve::Result<MaterialCatalog> create(
        std::vector<MaterialDefinition> definitions,
        std::vector<MaterialReactionRule> reactions = {},
        std::vector<MaterialPhaseRule> phaseRules = {});
    /** @brief Construct the engine's canonical built-in catalog. */
    static MaterialCatalog builtIn();

    /** @brief Resolve a stable name or return a structured NotFound diagnostic. */
    [[nodiscard]] eve::Result<MaterialId> resolve(std::string_view name) const;
    /** @brief Resolve a validated id; invalid ids safely project to air. */
    const MaterialDefinition& definition(MaterialId id) const noexcept;
    /** @brief Borrow immutable definitions until this catalog is destroyed. */
    std::span<const MaterialDefinition> definitions() const noexcept;
    /** @brief Borrow immutable canonical-priority reaction rules. */
    std::span<const MaterialReactionRule> reactions() const noexcept;
    /** @brief Borrow immutable canonical-priority phase rules. */
    std::span<const MaterialPhaseRule> phaseRules() const noexcept;
    /** @brief Whether this material participates in at least one binary reaction. */
    bool canReact(MaterialId id) const noexcept;
    /** @brief Whether this material owns at least one phase transition rule. */
    bool canPhaseTransition(MaterialId id) const noexcept;
    /** @brief Stable deterministic fingerprint included in world snapshots. */
    std::uint64_t fingerprint() const noexcept;

private:
    std::vector<MaterialDefinition> definitions_;
    std::vector<MaterialReactionRule> reactions_;
    std::vector<MaterialPhaseRule> phaseRules_;
    std::vector<std::uint8_t> reactionMask_;
    std::vector<std::uint8_t> phaseMask_;
    std::uint64_t fingerprint_ = 0;
};

}  // namespace eve::pixelworld
