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

/** @brief One pre-authored leaf bone (box collision proxy) inside a geometry collection. */
struct EVENGINE_API_DOMAINS GeometryCollectionBone {
    float halfExtentX = 0.5f;
    float halfExtentY = 0.5f;
    float halfExtentZ = 0.5f;
    float localX = 0.f;
    float localY = 0.f;
    float localZ = 0.f;
    float mass = 1.f;
    float density = 1.f;
    float friction = 0.4f;
    float restitution = 0.05f;
    bool  anchoredDefault = false;
    /** @brief Cluster island id from cook (Chaos Auto Cluster membership). */
    int clusterId = 0;
    /** @brief Fracture level; 0 = leaf. Higher levels reserved for hierarchy v2+. */
    int fractureLevel = 0;
};

/** @brief One undirected structural edge with a strain break threshold. */
struct EVENGINE_API_DOMAINS GeometryCollectionEdge {
    int   boneA = 0;
    int   boneB = 0;
    float strainThreshold = 1.f;
};

/**
 * @brief Immutable cooked/authored geometry-collection asset.
 *
 * Version 2 adds clusterId/fractureLevel on bones. Version 1 documents migrate
 * in fromValue (clusterId=0, fractureLevel=0). Unknown fields and unsupported
 * versions are rejected; there is no silent downgrade.
 */
struct EVENGINE_API_DOMAINS GeometryCollectionAsset {
    static constexpr std::string_view SchemaId      = "physics:geometry-collection";
    static constexpr std::uint32_t    SchemaVersion = 2;
    static constexpr std::uint32_t    SchemaVersionV1 = 1;

    std::vector<GeometryCollectionBone> bones;
    std::vector<GeometryCollectionEdge> edges;

    /** @brief Validate bone/edge topology and numeric ranges. */
    [[nodiscard("check geometry-collection asset validation")]] eve::Result<void> validate() const;
    /** @brief Encode the complete canonical version-2 value. */
    [[nodiscard("check geometry-collection asset encoding")]] eve::Result<eve::Value> toValue() const;
    /** @brief Build the tooling schema that mirrors validate(). */
    [[nodiscard]] static eve::schema::SchemaDefinition schemaDefinition();
    /** @brief Idempotently register version 2 in the process schema registry. */
    [[nodiscard("check geometry-collection schema registration")]] static eve::Result<void> ensureSchemaRegistered();
    /**
     * @brief Decode and validate version 2, or migrate version 1 (cluster fields = 0).
     *
     * Rejects unknown fields and unsupported versions; never partially publishes.
     */
    [[nodiscard("check geometry-collection asset decoding")]]
    static eve::Result<GeometryCollectionAsset> fromValue(const eve::Value& value);

    /** @brief Build a two-box welded fixture used by tests and the smoke example. */
    [[nodiscard("check welded-boxes fixture construction")]]
    static eve::Result<GeometryCollectionAsset> makeWeldedBoxesFixture(float strainThreshold = 1.f);

    /**
     * @brief Build a 2×3 anchored pillar with two cook clusters for demos.
     *
     * Bottom bones are anchored. Intra-cluster edges are stronger than the
     * inter-cluster welds so budgeted strain tends to split clusters first.
     */
    [[nodiscard("check cluster-pillar fixture construction")]]
    static eve::Result<GeometryCollectionAsset> makeClusterPillarFixture(float interClusterStrain = 0.8f);
};

}  // namespace eve::physics
