#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"
#include "procgen/road/RoadNetwork.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen::road {

/**
 * @brief One local primitive for a road decoration instance.
 *
 * Local frame: +X = road side (right when looking along forward), +Y = up,
 * +Z = along-road forward. Offsets and sizes are meters.
 */
struct RoadDecorPrimitive {
    enum class Shape : std::uint8_t { Box = 0, Cylinder = 1, Cone = 2 };

    Shape       shape  = Shape::Box;
    float       ox = 0.f, oy = 0.f, oz = 0.f;  ///< Local center offset.
    float       sx = 0.2f, sy = 0.5f, sz = 0.2f; ///< Box half-extents; cylinder/cone: sx=radius, sy=half-height.
    std::string group  = "decorCustom";          ///< MeshBuild triangle group name.
};

/**
 * @brief Placement + mesh recipe for one decoration kind along road edges.
 *
 * Built-ins (trees, lights, …) are expanded from RoadDecorOptions flags.
 * Callers add custom kinds through addCustomRoadDecor().
 */
struct RoadDecorSpec {
    std::string id;  ///< Stable id (e.g. "bench"); must be non-empty and unique in one options set.
    float       spacing           = 8.f;
    float       startOffset       = 1.5f;  ///< Skip this much after the usable tip.
    float       junctionClearance = 1.5f;  ///< Extra skip beyond junctionRadius at each end.
    float       lateralGap        = 0.6f;  ///< Outside halfWidth for roadside placement.
    float       lateral           = 0.f;   ///< Extra signed side offset (centerline mode).
    bool        roadside          = true;  ///< Place at ±(halfWidth + lateralGap).
    bool        bothSides         = true;  ///< If roadside, mirror to the left.
    bool        centerline        = false; ///< Place on the asphalt center (median props).
    float       yawJitterDeg      = 0.f;
    float       lateralJitter     = 0.f;
    std::uint32_t seedSalt        = 0;
    std::vector<RoadDecorPrimitive> parts;
};

/** @brief Optional roadside / median decorations baked with the road network. */
struct RoadDecorOptions {
    bool trees         = false;
    bool medianStrip   = false;  ///< Raised concrete island on bidirectional center.
    bool greenbelt     = false;  ///< Continuous grass strip outside the sidewalk.
    bool streetLights  = false;
    bool utilityPoles  = false;
    float treeSpacing  = 7.5f;
    float lightSpacing = 14.f;
    float poleSpacing  = 18.f;
    float greenbeltWidth = 1.35f;
    float medianWidth    = 1.0f;
    float medianHeight   = 0.28f;
    std::uint32_t seed   = 1;
    std::vector<RoadDecorSpec> custom;
};

/**
 * @brief Validate and append a custom decoration recipe.
 * @param options Mutated in place; ownership of @p spec moves into options.custom on success.
 * @return Success, or InvalidArgument when id/spacing/parts are invalid or id collides.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> addCustomRoadDecor(RoadDecorOptions& options, RoadDecorSpec spec);

/** @brief Built-in street-tree recipe (trunk + crown). */
[[nodiscard]] EVENGINE_API_DOMAINS RoadDecorSpec makeStreetTreeDecor(float spacing = 7.5f);
/** @brief Built-in street-light recipe (pole + arm + lamp). */
[[nodiscard]] EVENGINE_API_DOMAINS RoadDecorSpec makeStreetLightDecor(float spacing = 14.f);
/** @brief Built-in utility-pole recipe (shaft + crossarm). */
[[nodiscard]] EVENGINE_API_DOMAINS RoadDecorSpec makeUtilityPoleDecor(float spacing = 18.f);

/**
 * @brief Bake enabled built-in and custom decorations into @p mesh.
 * @note Skips placements inside junctionRadius (+ clearance) at each edge end.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> bakeRoadDecorations(MeshBuild& mesh, const RoadNetwork& network,
                                                                    const RoadDecorOptions& options);

}  // namespace eve::procgen::road
