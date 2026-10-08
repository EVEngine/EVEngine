#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Value.h"
#include "schema/SchemaTypes.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::physics {

class GeometryCollectionInstance;

/**
 * @brief Persistent runtime snapshot for one geometry-collection instance.
 *
 * Schema id `physics:geometry-collection-instance` version 1. Captures bone
 * states, edge strain/broken flags, and origin — not live PhysicsLink handles.
 * Restore rebuilds body types from state against a live World3D.
 *
 * Bone `state` uses the same integer encoding as BoneRuntimeState
 * (Attached=0, Detached=1, Sleeping=2).
 *
 * @ownership Owning value; never retains Body3D or World3D pointers.
 */
struct EVENGINE_API_DOMAINS GeometryCollectionInstanceSnapshot {
    static constexpr std::string_view SchemaId      = "physics:geometry-collection-instance";
    static constexpr std::uint32_t    SchemaVersion = 1;

    /** @brief BoneSnapshot public API. */
    struct BoneSnapshot {
        std::uint8_t state = 0;
        bool         anchored = false;
        int          clusterId = 0;
        int          fractureLevel = 0;
        float        halfExtentX = 0.5f;
        float        halfExtentY = 0.5f;
        float        halfExtentZ = 0.5f;
    };
    /** @brief EdgeSnapshot public API. */
    struct EdgeSnapshot {
        int   boneA = 0;
        int   boneB = 0;
        float strainThreshold = 1.f;
        float accumulatedStrain = 0.f;
        bool  broken = false;
    };

    float originX = 0.f;
    float originY = 0.f;
    float originZ = 0.f;
    std::int64_t lastTick = 0;
    std::uint64_t sleepBatchRevision = 0;
    std::vector<BoneSnapshot> bones;
    std::vector<EdgeSnapshot> edges;

    /** @brief Validate counts and numeric ranges. */
    [[nodiscard("check geometry-collection instance snapshot validation")]] eve::Result<void> validate() const;
    /** @brief Encode the complete canonical version-1 value. */
    [[nodiscard("check geometry-collection instance snapshot encoding")]] eve::Result<eve::Value> toValue() const;
    /** @brief Build the tooling schema that mirrors validate(). */
    [[nodiscard]] static eve::schema::SchemaDefinition schemaDefinition();
    /** @brief Idempotently register version 1 in the process schema registry. */
    [[nodiscard("check geometry-collection instance schema registration")]] static eve::Result<void>
    ensureSchemaRegistered();
    /** @brief Decode and validate version 1 transactionally. */
    [[nodiscard("check geometry-collection instance snapshot decoding")]]
    static eve::Result<GeometryCollectionInstanceSnapshot> fromValue(const eve::Value& value);
};

}  // namespace eve::physics
