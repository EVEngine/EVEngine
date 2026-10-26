#pragma once
#include "common/Export.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "building/BuildingTypes.h"
#include "common/Result.h"
#include "common/Value.h"

namespace eve::building {
struct PlacedBuilding;
struct EdgeCurveGroup;
/** @brief Persistent garrison member copied through building editor undo. */
struct BuildingGarrisonMemberRecord {
    std::string              id;
    std::string              type;
    std::vector<std::string> tags;
};

/** @brief Complete runtime-neutral placed-building snapshot. */
struct BuildingInstanceSnapshot {
    int         instanceId = 0;
    std::string buildingId;
    /** @brief Placement coordinate domain: `cell`, `edge`, `corner`, or `free`. */
    std::string placementKind = "cell";
    /** @brief Canonical edge origin when placementKind is `edge`. */
    int edgeX = 0;
    int edgeY = 0;
    /** @brief Canonical edge axis: `horizontal` or `vertical`. */
    std::string edgeAxis = "horizontal";
    /** @brief Grid vertex address when placementKind is `corner`. */
    int cornerX = 0;
    int cornerY = 0;
    /** @brief Committed free-domain collision radius in world-plane units. */
    double freeRadius = 0.0;
    /** @brief Committed free-domain OBB half width; zero denotes circular collision. */
    double freeHalfWidth = 0.0;
    /** @brief Committed free-domain OBB half height; zero denotes circular collision. */
    double freeHalfHeight = 0.0;
    /** @brief Committed convex local polygon as world-plane x/y pairs. */
    std::vector<double> freeFootprintVertices;
    int                 cellX = 0;
    int                 cellY = 0;
    /** @brief Authoritative discrete floor coordinate. */
    int    level     = 0;
    double worldX    = 0.0;
    double worldY    = 0.0;
    double elevation = 0.0;
    /** @brief Stable surface provider identity captured at placement time. */
    std::string surfaceId;
    /** @brief Surface provider revision captured at placement time. */
    std::uint64_t surfaceRevision = 0;
    /** @brief Captured unit surface normal. */
    double surfaceNormalX = 0.0;
    double surfaceNormalY = 1.0;
    double surfaceNormalZ = 0.0;
    /** @brief Captured unit surface tangent. */
    double surfaceTangentX = 1.0;
    double surfaceTangentY = 0.0;
    double surfaceTangentZ = 0.0;
    /** @brief Number of footprint surface samples represented by this snapshot. */
    int surfaceSampleCount = 0;
    /** @brief Maximum sampled footprint slope in degrees. */
    double surfaceMaxSlopeDegrees = 0.0;
    /** @brief Sampled footprint height range. */
    double                                    surfaceHeightDelta = 0.0;
    double                                    rotationDegrees    = 0.0;
    std::string                               channel;
    std::map<std::string, std::string>        properties;
    std::vector<std::string>                  tags;
    std::vector<BuildingGarrisonMemberRecord> garrison;
    std::uint64_t                             garrisonRevision = 0;
};


namespace codec {
/** @brief Owning curve group and all exact members, shared by runtime and editor persistence. */
struct EdgeCurvePayload {
    building::EdgeCurveGroup              group;
    std::vector<BuildingInstanceSnapshot> instances;
};

/** @brief Encode one complete curve group in linear time; no world mutation. */
EVENGINE_API_WORLD Value edgeCurveValue(const EdgeCurveGroup&                        group,
                                        const std::vector<BuildingInstanceSnapshot>& instances);
/** @brief Decode one complete curve with structured failure and independent ownership. */
[[nodiscard]] EVENGINE_API_WORLD Result<EdgeCurvePayload> parseEdgeCurve(const Value& value);

/** @brief Encode a complete owning instance; pure, caller-thread, linear in instance metadata. */
EVENGINE_API_WORLD Value instanceValue(const BuildingInstanceSnapshot& instance);
/** @brief Decode legacy/current instance fields without mutating a world; owning result. */
[[nodiscard]] EVENGINE_API_WORLD Result<BuildingInstanceSnapshot> parseInstance(const Value& value);
/** @brief Decode a nonempty instance batch with unique identities; pure owning result. */
[[nodiscard]] EVENGINE_API_WORLD Result<std::vector<BuildingInstanceSnapshot>> parseInstances(const Value& value);
/** @brief Copy a runtime instance into its canonical owning persistence record. */
EVENGINE_API_WORLD BuildingInstanceSnapshot snapshot(const PlacedBuilding& placed);
/** @brief Decode a previously validated snapshot into an owning runtime candidate. */
EVENGINE_API_WORLD PlacedBuilding runtime(const BuildingInstanceSnapshot& value);
}  // namespace codec
}  // namespace eve::building
