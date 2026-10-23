#pragma once

// Private geometry shared by placement translation units; not a module boundary.
#include "building/PlacementSystem.h"

namespace eve::building::placement_detail {
struct FreeFootprint {
    struct Point {
        float x = 0.f;
        float y = 0.f;
    };
    float              x               = 0.f;
    float              y               = 0.f;
    float              radius          = 0.f;
    float              halfWidth       = 0.f;
    float              halfHeight      = 0.f;
    float              rotationRadians = 0.f;
    std::vector<Point> vertices;

    bool isPolygon() const { return vertices.size() >= 3; }
};

bool          hasFreeEdgeOverlap(const PlacementWorld& world, const BuildingDefinition& freeDefinition,
                                 const FreeFootprint& footprint, const BuildingDefinition& edgeDefinition, EdgeAddress edge);
float         wrapDeg(float deg);
int           cardinalQuarter(float rotationDeg);
float         length3(float x, float y, float z);
FreeFootprint definitionFreeFootprint(const BuildingDefinition& definition, const PlacementWorld& world, float x,
                                      float y, float rotationDeg);
void          applyDefinitionFreeFootprint(PlacedBuilding& placed, const BuildingDefinition& definition,
                                           const PlacementWorld& world);
FreeFootprint placedFreeFootprint(const PlacedBuilding& placed);
std::vector<FreeFootprint::Point> worldVertices(const FreeFootprint& footprint);
bool                              isPointInPolygon(const FreeFootprint& polygon, float x, float y);
bool                              hasFreeFootprintOverlap(const FreeFootprint& a, const FreeFootprint& b);
bool                              containsFreeFootprintPoint(const FreeFootprint& footprint, float x, float y);
void cross3(float ax, float ay, float az, float bx, float by, float bz, float& x, float& y, float& z);
enum class SurfaceFrameStatus { Normalized, Invalid };
[[nodiscard]] SurfaceFrameStatus normalizeSurfaceFrame(PlacementSystem::PlacementHit& hit);
}  // namespace eve::building::placement_detail
