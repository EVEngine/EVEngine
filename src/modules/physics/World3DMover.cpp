#include "physics/World3DInternal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace eve::physics {
using namespace world3d_detail;

void World3D::setMoverUp(float x, float y, float z) { worldValue(rigidBackend_->setMoverUp(x, y, z)); }


void World3D::setMoverSlopeLimit(float degrees) {
    if (!std::isfinite(degrees)) throw eve::Exception("World3D.setMoverSlopeLimit: degrees must be finite");
    degrees        = std::clamp(degrees, 0.f, 89.9f);
    moverSlopeCos_ = std::cos(degrees * 0.01745329251994329577f);
}

bool World3D::moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx, float dy,
                          float dz) {
    return worldValue(rigidBackend_->moveCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz));
}


eve::Result<CapsuleMove3D> World3D::moveCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                     float radius, float dx, float dy, float dz, QueryFilter3D filter,
                                                     CapsuleMovePolicy3D policy) {
    return rigidBackend_->moveCapsuleOwned(ax, ay, az, bx, by, bz, radius, dx, dy, dz, filter, policy);
}


int World3D::getQueryBodyId(int index) const {
    if (index < 0 || index >= static_cast<int>(queryBodyIds_.size()))
        throw eve::Exception("World3D.getQueryBodyId: index out of range");
    return queryBodyIds_[static_cast<size_t>(index)];
}

bool World3D::pointProbe(float x, float y, float z, float radius, ClothContact3D *out) const {
    return pointProbeFiltered(x, y, z, radius, out, ~uint64_t{0}, ~uint64_t{0}) == ClothProbeStatus::Hit;
}

ClothProbeStatus World3D::pointProbeFiltered(float x, float y, float z, float radius, ClothContact3D *out,
                                             uint64_t categoryBits, uint64_t maskBits) const {
    if (!out) return ClothProbeStatus::Miss;
    *out = worldValue(rigidBackend_->pointProbeFiltered(x, y, z, radius, categoryBits, maskBits));
    return out->hit ? ClothProbeStatus::Hit : ClothProbeStatus::Miss;
}


const World3D::RayResult &World3D::rayResultAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(rayResults_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return rayResults_[static_cast<size_t>(index)];
}

#define EV_RAY_INT_GETTER(name, member) \
    int World3D::name(int index) const { return rayResultAt(index, #name).member; }

EV_RAY_INT_GETTER(getRayResultBodyId, bodyId)
EV_RAY_INT_GETTER(getRayResultShapeId, shapeId)
EV_RAY_INT_GETTER(getRayResultShapeTag, shapeTag)
EV_RAY_INT_GETTER(getRayResultMaterialId, materialId)
EV_RAY_INT_GETTER(getRayResultTriangleIndex, triangleIndex)

#undef EV_RAY_INT_GETTER

#define EV_RAY_FLOAT_GETTER(name, member) \
    float World3D::name(int index) const { return rayResultAt(index, #name).member; }

EV_RAY_FLOAT_GETTER(getRayResultX, x)
EV_RAY_FLOAT_GETTER(getRayResultY, y)
EV_RAY_FLOAT_GETTER(getRayResultZ, z)
EV_RAY_FLOAT_GETTER(getRayResultNormalX, normalX)
EV_RAY_FLOAT_GETTER(getRayResultNormalY, normalY)
EV_RAY_FLOAT_GETTER(getRayResultNormalZ, normalZ)
EV_RAY_FLOAT_GETTER(getRayResultFraction, fraction)

#undef EV_RAY_FLOAT_GETTER

const World3D::ShapeCastResult &World3D::shapeCastResultAt(int index, const char *operation) const {
    if (index < 0 || index >= static_cast<int>(shapeCastResults_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return shapeCastResults_[static_cast<size_t>(index)];
}

#define EV_CAST_INT_GETTER(name, member) \
    int World3D::name(int index) const { return shapeCastResultAt(index, #name).member; }

EV_CAST_INT_GETTER(getShapeCastResultBodyId, bodyId)
EV_CAST_INT_GETTER(getShapeCastResultShapeId, shapeId)
EV_CAST_INT_GETTER(getShapeCastResultShapeTag, shapeTag)
EV_CAST_INT_GETTER(getShapeCastResultMaterialId, materialId)
EV_CAST_INT_GETTER(getShapeCastResultTriangleIndex, triangleIndex)

#undef EV_CAST_INT_GETTER

#define EV_CAST_FLOAT_GETTER(name, member) \
    float World3D::name(int index) const { return shapeCastResultAt(index, #name).member; }

EV_CAST_FLOAT_GETTER(getShapeCastResultX, x)
EV_CAST_FLOAT_GETTER(getShapeCastResultY, y)
EV_CAST_FLOAT_GETTER(getShapeCastResultZ, z)
EV_CAST_FLOAT_GETTER(getShapeCastResultNormalX, normalX)
EV_CAST_FLOAT_GETTER(getShapeCastResultNormalY, normalY)
EV_CAST_FLOAT_GETTER(getShapeCastResultNormalZ, normalZ)
EV_CAST_FLOAT_GETTER(getShapeCastResultFraction, fraction)

#undef EV_CAST_FLOAT_GETTER

int World3D::getQueryShapeId(int index) const {
    if (index < 0 || index >= static_cast<int>(queryShapes_.size()))
        throw eve::Exception("World3D.getQueryShapeId: index out of range");
    return queryShapes_[static_cast<size_t>(index)].first;
}

int World3D::getQueryShapeTag(int index) const {
    if (index < 0 || index >= static_cast<int>(queryShapes_.size()))
        throw eve::Exception("World3D.getQueryShapeTag: index out of range");
    return queryShapes_[static_cast<size_t>(index)].second;
}

}  // namespace eve::physics
