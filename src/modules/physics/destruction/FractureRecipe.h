#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Value.h"
#include "schema/SchemaTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::physics {

/** @brief Fracture pattern used when cooking a geometry collection. */
enum class FractureMode : std::uint8_t {
    UniformVoronoi = 0,
    ClusteredVoronoi = 1,
    Radial = 2,
    Planar = 3,
};

/**
 * @brief Versioned cook recipe for Chaos-style pre-fracture (schema v1).
 *
 * Randomness comes only from `seed` mixed with `randomStreamName`; wall clock
 * is never read. Version 1 rejects unknown fields.
 */
struct EVENGINE_API_DOMAINS FractureRecipe {
    static constexpr std::string_view SchemaId      = "physics:fracture-recipe";
    static constexpr std::uint32_t    SchemaVersion = 1;

    FractureMode mode = FractureMode::UniformVoronoi;
    int          siteCountMin = 4;
    int          siteCountMax = 4;
    int          clusterCount = 2;
    float        clusterRadius = 0.35f;
    int          radialPlanes = 1;
    int          radialSpokes = 4;
    std::vector<float> planeNormals;  // xyz triples
    std::vector<float> planeOffsets;
    std::string  interiorMaterialLogicalId;
    std::string  randomStreamName = "destruction.fracture";
    std::uint64_t seed = 1;
    float        defaultStrainThreshold = 1.f;
    float        defaultDensity = 1.f;
    float        defaultFriction = 0.4f;
    float        defaultRestitution = 0.05f;
    /** @brief Minimum AABB axis length in metres; thinner meshes are rejected. */
    float        minimumThickness = 0.05f;
    /** @brief Soft cap on produced leaf bones. */
    int          maximumBones = 256;

    /** @brief Validate. */
    [[nodiscard("check fracture recipe validation")]] eve::Result<void> validate() const;
    /** @brief To value. */
    [[nodiscard("check fracture recipe encoding")]] eve::Result<eve::Value> toValue() const;
    /** @brief Schema definition. */
    [[nodiscard]] static eve::schema::SchemaDefinition schemaDefinition();
    /** @brief Ensure schema registered. */
    [[nodiscard("check fracture recipe schema registration")]] static eve::Result<void> ensureSchemaRegistered();
    [[nodiscard("check fracture recipe decoding")]]
    /** @brief From value. */
    static eve::Result<FractureRecipe> fromValue(const eve::Value& value);
};

}  // namespace eve::physics
