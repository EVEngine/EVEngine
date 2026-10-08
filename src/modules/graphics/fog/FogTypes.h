#pragma once
#include "common/Export.h"

#include <cstdint>
#include <string_view>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace eve::graphics::fog {

/** @brief Tracking / cache / skip budget for the realtime fog pipeline. */
enum class FogQuality : uint8_t {
    Fast = 0,               ///< Low sample count, Beer cache, occupancy skip.
    Enhanced = 1,           ///< Temporal + spatial reconstruction, Beer cache.
    PhysicalReference = 2,  ///< Dense tracking, no skip shortcuts.
};

/** @brief Stable spelling of a quality preset. */
[[nodiscard]] inline constexpr std::string_view fogQualityName(FogQuality quality) noexcept {
    switch (quality) {
        case FogQuality::Fast: return "fast";
        case FogQuality::Enhanced: return "enhanced";
        case FogQuality::PhysicalReference: return "physical_reference";
    }
    return "unknown";
}

/** @brief Parse a quality spelling; unknown values yield Enhanced. */
[[nodiscard]] inline FogQuality fogQualityFromName(std::string_view name) noexcept {
    if (name == "fast" || name == "low") return FogQuality::Fast;
    if (name == "physical_reference" || name == "physical" || name == "high")
        return FogQuality::PhysicalReference;
    return FogQuality::Enhanced;
}

/** @brief Resource / sample budget derived from a quality preset. */
struct FogQualityBudget {
    int raySamples = 24;
    int volLightSegments = 4;
    int froxelWidth = 80;
    int froxelHeight = 45;
    int froxelDepth = 48;
    bool beerLightCache = true;
    bool occupancySkip = true;
    bool temporalHistory = true;
    bool spatialReconstruct = false;
    float historyWeight = 0.75f;
};

/** @brief Resolve the budget for a quality preset. */
[[nodiscard]] inline FogQualityBudget budgetFor(FogQuality quality) noexcept {
    FogQualityBudget budget;
    switch (quality) {
        case FogQuality::Fast:
            budget.raySamples = 12;
            budget.volLightSegments = 2;
            budget.froxelWidth = 48;
            budget.froxelHeight = 27;
            budget.froxelDepth = 32;
            budget.beerLightCache = true;
            budget.occupancySkip = true;
            budget.temporalHistory = true;
            budget.spatialReconstruct = false;
            budget.historyWeight = 0.85f;
            break;
        case FogQuality::Enhanced:
            budget.raySamples = 24;
            budget.volLightSegments = 4;
            budget.froxelWidth = 80;
            budget.froxelHeight = 45;
            budget.froxelDepth = 48;
            budget.beerLightCache = true;
            budget.occupancySkip = true;
            budget.temporalHistory = true;
            budget.spatialReconstruct = true;
            budget.historyWeight = 0.75f;
            break;
        case FogQuality::PhysicalReference:
            budget.raySamples = 64;
            budget.volLightSegments = 8;
            budget.froxelWidth = 128;
            budget.froxelHeight = 72;
            budget.froxelDepth = 64;
            budget.beerLightCache = false;
            budget.occupancySkip = false;
            budget.temporalHistory = false;
            budget.spatialReconstruct = false;
            budget.historyWeight = 0.f;
            break;
    }
    return budget;
}

/** @brief Axis-aligned world bounds for a fog simulation domain. */
struct FogWorldBounds {
    glm::vec3 minimum{-8.f, 0.f, -8.f};
    glm::vec3 maximum{8.f, 6.f, 8.f};

    [[nodiscard]] glm::vec3 size() const noexcept { return maximum - minimum; }
    [[nodiscard]] glm::vec3 center() const noexcept { return 0.5f * (minimum + maximum); }
    [[nodiscard]] bool contains(const glm::vec3& p) const noexcept {
        return p.x >= minimum.x && p.y >= minimum.y && p.z >= minimum.z && p.x <= maximum.x &&
               p.y <= maximum.y && p.z <= maximum.z;
    }
};

/** @brief Interval along a ray where media is present (t in world units). */
struct FogRayInterval {
    float tEnter = 0.f;
    float tExit = 0.f;
    [[nodiscard]] bool valid() const noexcept { return tExit > tEnter; }
    [[nodiscard]] float length() const noexcept { return tExit - tEnter; }
};

/** @brief Optical result of integrating one view ray through fog. */
struct FogRayResult {
    glm::vec3 inScatter{0.f};
    float transmittance = 1.f;
    float opticalDepth = 0.f;
    int samplesUsed = 0;
};

/** @brief CFL stability snapshot for the MAC fluid solver. */
struct FogCflReport {
    float maxSpeed = 0.f;
    float cellSize = 0.f;
    float dt = 0.f;
    float cfl = 0.f;
    bool stable = true;
};

/** @brief Solid proxy kinds accepted by the realtime fog interactor. */
enum class FogProxyShape : uint8_t { Sphere = 0, Capsule = 1, Obb = 2 };

/** @brief One analytic solid that carves / wakes the fog domain. */
struct FogSolidProxy {
    FogProxyShape shape = FogProxyShape::Sphere;
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    /** @brief Sphere/capsule radius, or half-extents for OBB. */
    glm::vec3 extents{0.5f};
    /** @brief Capsule axis endpoint offset from position (half-length along axis). */
    glm::vec3 axis{0.f, 0.5f, 0.f};
    /** @brief OBB local axes (columns); identity when unused. */
    glm::vec3 axisX{1.f, 0.f, 0.f};
    glm::vec3 axisY{0.f, 1.f, 0.f};
    glm::vec3 axisZ{0.f, 0.f, 1.f};
    float drag = 0.35f;
    float wakeStrength = 1.f;
    bool enabled = true;
};

/** @brief Continuous-art stylization that never mutates density or optics. */
struct ContinuousArtParams {
    glm::vec3 colorTint{1.f};
    float edgeBoost = 0.f;
    float silhouetteSoftness = 0.25f;
    float internalGlow = 0.f;
    float silverDust = 0.f;
};

/** @brief Stylized fog presentation derived from a physical FogRayResult. */
struct ContinuousArtOutput {
    glm::vec3 color{0.f};
    float opacity = 0.f;
    float edgeMask = 0.f;
    float dustHighlight = 0.f;
};

}  // namespace eve::graphics::fog
