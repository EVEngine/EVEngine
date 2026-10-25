#pragma once

#include "procgen/MeshBuild.h"

namespace eve::procgen::prototype_detail {

/** @brief Vec3 public API. */
struct Vec3 {
    float x;
    float y;
    float z;
};

/** @brief Adds box. */
void addBox(MeshBuild& mesh, Vec3 center, Vec3 size, const char* group);
/** @brief Adds wedge. */
void addWedge(MeshBuild& mesh, Vec3 center, Vec3 size, bool riseAlongX, const char* group);
/** @brief Adds cylinder. */
void addCylinder(MeshBuild& mesh, Vec3 center, float radiusBottom, float radiusTop, float height, int sides,
                 const char* group);
/** @brief Adds sphere. */
void addSphere(MeshBuild& mesh, Vec3 center, Vec3 radii, int rings, int sides, const char* group);
/** @brief Adds torus. */
void addTorus(MeshBuild& mesh, Vec3 center, float majorRadius, float minorRadius, int majorSegments, int minorSegments,
              const char* group);

}  // namespace eve::procgen::prototype_detail
