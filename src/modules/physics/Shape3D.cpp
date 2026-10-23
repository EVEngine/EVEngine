#include "physics/Shape3D.h"
#include "common/Assert.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include "common/Exception.h"


#include <cstdint>
#include <cmath>
#include <utility>

namespace eve::physics {
namespace {

constexpr uint64_t materialIdMask = 0xFFFFFFFFull;
constexpr int frictionModeShift = 32;
constexpr int restitutionModeShift = 35;
constexpr uint64_t combineModeMask = 0x7ull;

uint64_t parseCombineMode(const std::string &mode, const char *operation) {
    if (mode == "default") return 0;
    if (mode == "average") return 1;
    if (mode == "minimum" || mode == "min") return 2;
    if (mode == "multiply") return 3;
    if (mode == "maximum" || mode == "max") return 4;
    throw eve::Exception("%s: mode must be default|average|minimum|multiply|maximum",
                         operation);
}

std::string combineModeName(uint64_t mode) {
    switch (mode) {
        case 0: return "default";
        case 1: return "average";
        case 2: return "minimum";
        case 3: return "multiply";
        case 4: return "maximum";
        default: return "default";
    }
}

template <class T>
T shapeValue(eve::Result<T> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
    return std::move(result).value();
}
void shapeValue(eve::Result<void> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
}
}  // namespace

Shape3D::Shape3D(World3D *world, Body3D *body, std::unique_ptr<IShapeBackend3D> backend,
                 PhysicsShapeHandle runtimeHandle, ShapeGeometry3D geometry)
    : world_(world),
      body_(body),
      backend_(std::move(backend)),
      runtimeHandle_(runtimeHandle),
      geometry_(std::move(geometry)),
      id_(world ? world->nextShapeId() : 0) {
    EV_PARAM_CHECK(backend_ != nullptr, "Shape3D requires a backend");
    backend_->bindOwner(*this);
    if (geometry_.kind == Kind::TriangleMesh) {
        const int count = shapeValue(backend_->meshMaterialCount());
        for (int i = 0; i < count; ++i) meshMaterials_.push_back(shapeValue(backend_->meshMaterial(i)));
    }
    if (world_) world_->registerShapeHandle(this);
}
Shape3D::~Shape3D() { destroy(); }
bool Shape3D::isValid() const { return backend_ && backend_->isValid(); }
void Shape3D::invalidate() {
    if (backend_) backend_->invalidate();
    if (world_) world_->forgetShape(this);
    body_          = nullptr;
    world_         = nullptr;
    runtimeHandle_ = PhysicsShapeHandle::invalid();
}
void Shape3D::destroy() {
    if (backend_) backend_->destroy();
    invalidate();
}
void Shape3D::rebuildGeometry(const ShapeGeometry3D &geometry, bool sensor) {
    if (!body_ || !body_->isValid()) throw eve::Exception("Shape3D: cannot rebuild geometry after body destruction");
    if (geometry.kind == Kind::TriangleMesh && isValid() && !meshMaterialsDirty_) {
        const int                      count = shapeValue(backend_->meshMaterialCount());
        std::vector<SurfaceMaterial3D> refreshed;
        refreshed.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) refreshed.push_back(shapeValue(backend_->meshMaterial(i)));
        meshMaterials_ = std::move(refreshed);
    }
    shapeValue(backend_->rebuild(
        geometry, {sensor, hitEventsEnabled_, oneWayEnabled_, contactOverrideFiltering_, explosionScale_},
        meshMaterials_));
    meshMaterialsDirty_ = false;
    if (world_) world_->registerShapeHandle(this);
}
void Shape3D::recreate(bool sensor) { rebuildGeometry(geometry_, sensor); }
void Shape3D::applyGeometry(ShapeGeometry3D geometry, bool sensor) {
    rebuildGeometry(geometry, sensor);
    geometry_ = std::move(geometry);
}

void Shape3D::setSensor(bool sensor) {
    if (!body_ || !body_->isValid()) return;
    if (isValid() && shapeValue(backend_->isSensor()) == sensor) return;
    recreate(sensor);
}

void Shape3D::enableContactOverrideFiltering() {
    if (contactOverrideFiltering_) return;
    const bool sensor         = isSensor();
    contactOverrideFiltering_ = true;
    try {
        recreate(sensor);
    } catch (...) {
        contactOverrideFiltering_ = false;
        throw;
    }
}

void Shape3D::setTag(int tag) {
    tag_ = tag;
    if (world_) world_->updateShapeTag(this);
}

void Shape3D::setHitEventsEnabled(bool enabled) {
    if (isValid()) shapeValue(backend_->enableHitEvents(enabled && !isSensor()));
    hitEventsEnabled_ = enabled;
}

bool Shape3D::areHitEventsEnabled() const {
    return hitEventsEnabled_;
}

void Shape3D::setOneWay(float nx, float ny, float nz, float planeOffset, float margin,
                        float minNormalDot) {
    if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) ||
        !std::isfinite(planeOffset) || !std::isfinite(margin) ||
        !std::isfinite(minNormalDot))
        throw eve::Exception("Shape3D.setOneWay: all values must be finite");
    const float lengthSquared = nx * nx + ny * ny + nz * nz;
    if (!(lengthSquared > 1e-12f))
        throw eve::Exception("Shape3D.setOneWay: normal must be non-zero");
    if (margin < 0.f)
        throw eve::Exception("Shape3D.setOneWay: margin must be >= 0");
    if (minNormalDot < -1.f || minNormalDot > 1.f)
        throw eve::Exception("Shape3D.setOneWay: minNormalDot must be in [-1,1]");
    if (isValid()) shapeValue(backend_->enablePreSolveEvents(!isSensor()));
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    oneWayLocalNormalX_ = nx * inverseLength;
    oneWayLocalNormalY_ = ny * inverseLength;
    oneWayLocalNormalZ_ = nz * inverseLength;
    oneWayPlaneOffset_ = planeOffset;
    oneWayMargin_ = margin;
    oneWayMinNormalDot_ = minNormalDot;
    oneWayEnabled_            = true;
}

void Shape3D::disableOneWay() {
    if (isValid()) shapeValue(backend_->enablePreSolveEvents(false));
    oneWayEnabled_ = false;
}

void Shape3D::refreshOneWayWorldData() {
    if (!oneWayEnabled_ || !body_ || !body_->isValid()) return;
    const auto plane    = shapeValue(backend_->oneWayPlane(
        geometry_, {oneWayLocalNormalX_, oneWayLocalNormalY_, oneWayLocalNormalZ_}, oneWayPlaneOffset_));
    oneWayWorldPointX_  = plane.x;
    oneWayWorldPointY_  = plane.y;
    oneWayWorldPointZ_  = plane.z;
    oneWayWorldNormalX_ = plane.nx;
    oneWayWorldNormalY_ = plane.ny;
    oneWayWorldNormalZ_ = plane.nz;
}

bool Shape3D::allowsOneWayContact(double pointX, double pointY, double pointZ, float normalX,
                                  float normalY, float normalZ) const {
    if (!oneWayEnabled_) return true;
    const double side = (pointX - oneWayWorldPointX_) * oneWayWorldNormalX_ +
                        (pointY - oneWayWorldPointY_) * oneWayWorldNormalY_ +
                        (pointZ - oneWayWorldPointZ_) * oneWayWorldNormalZ_;
    const float alignment = normalX * oneWayWorldNormalX_ + normalY * oneWayWorldNormalY_ +
                            normalZ * oneWayWorldNormalZ_;
    return side >= -static_cast<double>(oneWayMargin_) && alignment >= oneWayMinNormalDot_;
}

std::string Shape3D::getKind() const {
    switch (geometry_.kind) {
        case Kind::Box: return "box";
        case Kind::Sphere: return "sphere";
        case Kind::Capsule: return "capsule";
        case Kind::ConvexHull: return "convexHull";
        case Kind::TriangleMesh: return "triangleMesh";
        case Kind::HeightField: return "heightField";
    }
    return "box";
}

void Shape3D::setBoxSize(float width, float height, float depth) {
    if (geometry_.kind != Kind::Box) throw eve::Exception("Shape3D.setBoxSize: shape is not a box");
    if (!(width > 0.f) || !(height > 0.f) || !(depth > 0.f) || !std::isfinite(width) ||
        !std::isfinite(height) || !std::isfinite(depth))
        throw eve::Exception("Shape3D.setBoxSize: dimensions must be finite and > 0");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setBoxSize: body destroyed");
    const float halfWidth = 0.5f * width;
    const float halfHeight = 0.5f * height;
    const float halfDepth = 0.5f * depth;
    if (geometry_.a == halfWidth && geometry_.b == halfHeight && geometry_.c == halfDepth) return;
    const bool sensor = isSensor();
    auto       candidate = geometry_;
    candidate.a          = halfWidth;
    candidate.b          = halfHeight;
    candidate.c          = halfDepth;
    applyGeometry(std::move(candidate), sensor);
}

void Shape3D::setSphereRadius(float radius) {
    if (geometry_.kind != Kind::Sphere) throw eve::Exception("Shape3D.setSphereRadius: shape is not a sphere");
    if (!(radius > 0.f) || !std::isfinite(radius))
        throw eve::Exception("Shape3D.setSphereRadius: radius must be finite and > 0");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setSphereRadius: body destroyed");
    if (geometry_.a == radius) return;
    const bool sensor = isSensor();
    auto       candidate = geometry_;
    candidate.a          = radius;
    applyGeometry(std::move(candidate), sensor);
}

void Shape3D::setCapsuleSize(float height, float radius) {
    if (geometry_.kind != Kind::Capsule) throw eve::Exception("Shape3D.setCapsuleSize: shape is not a capsule");
    if (!(height >= 0.f) || !(radius > 0.f) || !std::isfinite(height) ||
        !std::isfinite(radius))
        throw eve::Exception(
            "Shape3D.setCapsuleSize: height must be finite and >= 0; radius finite and > 0");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setCapsuleSize: body destroyed");
    const float halfHeight = 0.5f * height;
    if (geometry_.a == halfHeight && geometry_.b == radius) return;
    const bool sensor = isSensor();
    auto       candidate = geometry_;
    candidate.a          = halfHeight;
    candidate.b          = radius;
    applyGeometry(std::move(candidate), sensor);
}

void Shape3D::setConvexHullVertices(const std::vector<float> &vertices, int maxVertices) {
    if (geometry_.kind != Kind::ConvexHull)
        throw eve::Exception("Shape3D.setConvexHullVertices: shape is not a convex hull");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setConvexHullVertices: body destroyed");
    if (vertices == geometry_.hullVertices && maxVertices == geometry_.hullMaxVertices) return;
    const bool sensor = isSensor();
    auto       candidate      = geometry_;
    candidate.hullVertices    = vertices;
    candidate.hullMaxVertices = maxVertices;
    applyGeometry(std::move(candidate), sensor);
}

void Shape3D::setTriangleMeshData(const std::vector<float> &vertices,
                                  const std::vector<int32_t> &indices, bool weldVertices,
                                  float weldTolerance, bool identifyEdges,
                                  bool useMedianSplit) {
    if (geometry_.kind != Kind::TriangleMesh)
        throw eve::Exception("Shape3D.setTriangleMeshData: shape is not a triangle mesh");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setTriangleMeshData: body destroyed");
    if (body_->getType() != "static")
        throw eve::Exception("Shape3D.setTriangleMeshData: triangle meshes require a static body");
    if (vertices == geometry_.meshVertices && indices == geometry_.meshIndices &&
        weldVertices == geometry_.meshWeldVertices && weldTolerance == geometry_.meshWeldTolerance &&
        identifyEdges == geometry_.meshIdentifyEdges && useMedianSplit == geometry_.meshUseMedianSplit)
        return;
    const bool sensor = isSensor();
    auto       candidate         = geometry_;
    candidate.meshVertices       = vertices;
    candidate.meshIndices        = indices;
    candidate.meshWeldVertices   = weldVertices;
    candidate.meshWeldTolerance  = weldTolerance;
    candidate.meshIdentifyEdges  = identifyEdges;
    candidate.meshUseMedianSplit = useMedianSplit;
    const auto previousMaterials = meshMaterials_;
    const bool previousDirty     = meshMaterialsDirty_;
    if (candidate.meshMaterialIndices.size() != indices.size() / 3) {
        candidate.meshMaterialIndices.clear();
        if (meshMaterials_.size() > 1) meshMaterials_.resize(1);
        meshMaterialsDirty_ = true;
    }
    try {
        applyGeometry(std::move(candidate), sensor);
    } catch (...) {
        meshMaterials_      = previousMaterials;
        meshMaterialsDirty_ = previousDirty;
        throw;
    }
}

void Shape3D::setTriangleMeshMaterialIndices(
    const std::vector<int32_t> &materialIndices) {
    if (geometry_.kind != Kind::TriangleMesh)
        throw eve::Exception(
            "Shape3D.setTriangleMeshMaterialIndices: shape is not a triangle mesh");
    const size_t triangleCount = geometry_.meshIndices.size() / 3;
    if (materialIndices.size() != triangleCount)
        throw eve::Exception(
            "Shape3D.setTriangleMeshMaterialIndices: one index is required per triangle");
    std::vector<uint8_t> encoded;
    encoded.reserve(materialIndices.size());
    int requiredCount = 1;
    for (int32_t index : materialIndices) {
        if (index < 0 || index > 254)
            throw eve::Exception(
                "Shape3D.setTriangleMeshMaterialIndices: indices must be in [0, 254]");
        encoded.push_back(static_cast<uint8_t>(index));
        requiredCount = std::max(requiredCount, static_cast<int>(index) + 1);
    }
    if (encoded == geometry_.meshMaterialIndices && requiredCount == static_cast<int>(meshMaterials_.size())) return;
    const bool sensor = isSensor();
    const auto oldIndices         = geometry_.meshMaterialIndices;
    const auto oldMaterials = meshMaterials_;
    geometry_.meshMaterialIndices = std::move(encoded);
    if (meshMaterials_.empty()) meshMaterials_.push_back(SurfaceMaterial3D{});
    meshMaterials_.resize(static_cast<size_t>(requiredCount), meshMaterials_.front());
    meshMaterialsDirty_ = true;
    try {
        recreate(sensor);
    } catch (...) {
        geometry_.meshMaterialIndices = oldIndices;
        meshMaterials_ = oldMaterials;
        meshMaterialsDirty_           = false;
        throw;
    }
}

int Shape3D::getTriangleMeshMaterialIndex(int triangleIndex) const {
    if (geometry_.kind != Kind::TriangleMesh)
        throw eve::Exception(
            "Shape3D.getTriangleMeshMaterialIndex: shape is not a triangle mesh");
    const int triangleCount = static_cast<int>(geometry_.meshIndices.size() / 3);
    if (triangleIndex < 0 || triangleIndex >= triangleCount)
        throw eve::Exception("Shape3D.getTriangleMeshMaterialIndex: triangle out of range");
    return geometry_.meshMaterialIndices.empty() ? 0
                                                 : geometry_.meshMaterialIndices[static_cast<size_t>(triangleIndex)];
}

int Shape3D::getTriangleMeshMaterialCount() const {
    return geometry_.kind == Kind::TriangleMesh ? std::max(1, static_cast<int>(meshMaterials_.size())) : 0;
}

void Shape3D::setTriangleMeshMaterial(int slot, float friction, float restitution,
                                      float rollingResistance, float tangentX,
                                      float tangentY, float tangentZ, int materialId,
                                      const std::string &frictionMode,
                                      const std::string &restitutionMode) {
    if (geometry_.kind != Kind::TriangleMesh)
        throw eve::Exception("Shape3D.setTriangleMeshMaterial: shape is not a triangle mesh");
    if (slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.setTriangleMeshMaterial: slot out of range");
    if (!std::isfinite(friction) || friction < 0.f || !std::isfinite(restitution) ||
        restitution < 0.f || !std::isfinite(rollingResistance) || rollingResistance < 0.f ||
        !std::isfinite(tangentX) || !std::isfinite(tangentY) || !std::isfinite(tangentZ))
        throw eve::Exception(
            "Shape3D.setTriangleMeshMaterial: numeric values must be finite and non-negative where required");
    const uint64_t frictionCode =
        parseCombineMode(frictionMode, "Shape3D.setTriangleMeshMaterial");
    const uint64_t restitutionCode =
        parseCombineMode(restitutionMode, "Shape3D.setTriangleMeshMaterial");
    SurfaceMaterial3D material = SurfaceMaterial3D{};
    material.friction = friction;
    material.restitution = restitution;
    material.rollingResistance = rollingResistance;
    material.tangentVelocity = {tangentX, tangentY, tangentZ};
    material.userMaterialId = static_cast<uint32_t>(materialId) |
                              (frictionCode << frictionModeShift) |
                              (restitutionCode << restitutionModeShift);
    if (meshMaterials_.empty()) meshMaterials_.push_back(SurfaceMaterial3D{});
    if (isValid()) shapeValue(backend_->setMeshMaterial(material, slot));
    meshMaterials_[static_cast<size_t>(slot)] = material;
}

float Shape3D::getTriangleMeshMaterialFriction(int slot) const {
    if (geometry_.kind != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.getTriangleMeshMaterialFriction: slot out of range");
    return meshMaterials_.empty() ? getFriction() : meshMaterials_[static_cast<size_t>(slot)].friction;
}

float Shape3D::getTriangleMeshMaterialRestitution(int slot) const {
    if (geometry_.kind != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.getTriangleMeshMaterialRestitution: slot out of range");
    return meshMaterials_.empty() ? getRestitution() : meshMaterials_[static_cast<size_t>(slot)].restitution;
}

float Shape3D::getTriangleMeshMaterialRollingResistance(int slot) const {
    if (geometry_.kind != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception(
            "Shape3D.getTriangleMeshMaterialRollingResistance: slot out of range");
    return meshMaterials_.empty() ? getRollingResistance()
                                  : meshMaterials_[static_cast<size_t>(slot)].rollingResistance;
}

int Shape3D::getTriangleMeshMaterialId(int slot) const {
    if (geometry_.kind != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.getTriangleMeshMaterialId: slot out of range");
    const uint64_t id = meshMaterials_.empty() ? shapeValue(backend_->material()).userMaterialId
                                               : meshMaterials_[static_cast<size_t>(slot)].userMaterialId;
    return static_cast<int>(static_cast<uint32_t>(id));
}

void Shape3D::setHeightFieldHeights(const std::vector<float> &heights) {
    if (geometry_.kind != Kind::HeightField)
        throw eve::Exception("Shape3D.setHeightFieldHeights: shape is not a height field");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setHeightFieldHeights: body destroyed");
    if (heights == geometry_.heightValues) return;
    const bool sensor = isSensor();
    auto       candidate   = geometry_;
    candidate.heightValues = heights;
    applyGeometry(std::move(candidate), sensor);
}

void Shape3D::setHeightFieldRegion(int x, int z, int width, int depth,
                                   const std::vector<float> &heights) {
    if (geometry_.kind != Kind::HeightField)
        throw eve::Exception("Shape3D.setHeightFieldRegion: shape is not a height field");
    if (x < 0 || z < 0 || width < 1 || depth < 1 || x > geometry_.heightCountX - width ||
        z > geometry_.heightCountZ - depth)
        throw eve::Exception("Shape3D.setHeightFieldRegion: region is outside the sample grid");
    if (heights.size() != static_cast<size_t>(width) * static_cast<size_t>(depth))
        throw eve::Exception("Shape3D.setHeightFieldRegion: data size must equal width * depth");
    std::vector<float> updated = geometry_.heightValues;
    for (int row = 0; row < depth; ++row) {
        for (int column = 0; column < width; ++column) {
            updated[static_cast<size_t>(z + row) * static_cast<size_t>(geometry_.heightCountX) +
                    static_cast<size_t>(x + column)] =
                heights[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column)];
        }
    }
    setHeightFieldHeights(updated);
}

float Shape3D::getHeightFieldHeight(int x, int z) const {
    if (geometry_.kind != Kind::HeightField)
        throw eve::Exception("Shape3D.getHeightFieldHeight: shape is not a height field");
    if (x < 0 || x >= geometry_.heightCountX || z < 0 || z >= geometry_.heightCountZ)
        throw eve::Exception("Shape3D.getHeightFieldHeight: sample index out of range");
    return geometry_
        .heightValues[static_cast<size_t>(z) * static_cast<size_t>(geometry_.heightCountX) + static_cast<size_t>(x)];
}

void Shape3D::setLocalTransform(float px, float py, float pz, float qx, float qy, float qz,
                                float qw) {
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setLocalTransform: body destroyed");
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
        !std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) ||
        !std::isfinite(qw))
        throw eve::Exception("Shape3D.setLocalTransform: values must be finite");
    const float lengthSquared = qx * qx + qy * qy + qz * qz + qw * qw;
    if (!(lengthSquared > 1e-12f))
        throw eve::Exception("Shape3D.setLocalTransform: quaternion must be non-zero");
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    const float normalizedQx = qx * inverseLength;
    const float normalizedQy = qy * inverseLength;
    const float normalizedQz = qz * inverseLength;
    const float normalizedQw = qw * inverseLength;
    if (geometry_.kind == Kind::HeightField && (px != 0.f || py != 0.f || pz != 0.f || normalizedQx != 0.f ||
                                                normalizedQy != 0.f || normalizedQz != 0.f || normalizedQw != 1.f))
        throw eve::Exception(
            "Shape3D.setLocalTransform: height fields use their Body transform and require an identity local transform");
    if (geometry_.localX == px && geometry_.localY == py && geometry_.localZ == pz &&
        geometry_.localQx == normalizedQx && geometry_.localQy == normalizedQy && geometry_.localQz == normalizedQz &&
        geometry_.localQw == normalizedQw)
        return;

    const bool sensor = isSensor();
    auto       candidate = geometry_;
    candidate.localX     = px;
    candidate.localY     = py;
    candidate.localZ     = pz;
    candidate.localQx    = normalizedQx;
    candidate.localQy    = normalizedQy;
    candidate.localQz    = normalizedQz;
    candidate.localQw    = normalizedQw;
    applyGeometry(std::move(candidate), sensor);
}

void Shape3D::setLocalPosition(float x, float y, float z) {
    setLocalTransform(x, y, z, geometry_.localQx, geometry_.localQy, geometry_.localQz, geometry_.localQw);
}

void Shape3D::setLocalRotation(float qx, float qy, float qz, float qw) {
    setLocalTransform(geometry_.localX, geometry_.localY, geometry_.localZ, qx, qy, qz, qw);
}

bool Shape3D::isSensor() const { return isValid() ? shapeValue(backend_->isSensor()) : false; }

void Shape3D::setFriction(float friction) {
    if (!std::isfinite(friction) || friction < 0.f)
        throw eve::Exception("Shape3D.setFriction: friction must be finite and >= 0");
    if (!isValid()) return;
    shapeValue(backend_->setFriction(friction));
}

float Shape3D::getFriction() const { return isValid() ? shapeValue(backend_->friction()) : 0.f; }

void Shape3D::setRestitution(float restitution) {
    if (!std::isfinite(restitution) || restitution < 0.f)
        throw eve::Exception("Shape3D.setRestitution: restitution must be finite and >= 0");
    if (!isValid()) return;
    shapeValue(backend_->setRestitution(restitution));
}

float Shape3D::getRestitution() const { return isValid() ? shapeValue(backend_->restitution()) : 0.f; }

void Shape3D::setRollingResistance(float resistance) {
    if (!std::isfinite(resistance) || resistance < 0.f)
        throw eve::Exception(
            "Shape3D.setRollingResistance: resistance must be finite and >= 0");
    if (!isValid()) return;
    SurfaceMaterial3D material = shapeValue(backend_->material());
    material.rollingResistance = resistance;
    shapeValue(backend_->setMaterial(material));
}

float Shape3D::getRollingResistance() const {
    return isValid() ? shapeValue(backend_->material()).rollingResistance : 0.f;
}

void Shape3D::setTangentVelocity(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("Shape3D.setTangentVelocity: components must be finite");
    if (!isValid()) return;
    SurfaceMaterial3D material = shapeValue(backend_->material());
    material.tangentVelocity   = PhysicsVector3D{x, y, z};
    shapeValue(backend_->setMaterial(material));
}

float Shape3D::getTangentVelocityX() const {
    return isValid() ? shapeValue(backend_->material()).tangentVelocity.x : 0.f;
}

float Shape3D::getTangentVelocityY() const {
    return isValid() ? shapeValue(backend_->material()).tangentVelocity.y : 0.f;
}

float Shape3D::getTangentVelocityZ() const {
    return isValid() ? shapeValue(backend_->material()).tangentVelocity.z : 0.f;
}

void Shape3D::setMaterialId(int id) {
    if (!isValid()) return;
    SurfaceMaterial3D material = shapeValue(backend_->material());
    material.userMaterialId =
        (material.userMaterialId & ~materialIdMask) | static_cast<uint32_t>(id);
    shapeValue(backend_->setMaterial(material));
}

void Shape3D::setFrictionCombineMode(const std::string &mode) {
    const uint64_t encoded = parseCombineMode(mode, "Shape3D.setFrictionCombineMode");
    if (!isValid()) return;
    SurfaceMaterial3D material = shapeValue(backend_->material());
    material.userMaterialId &= ~(combineModeMask << frictionModeShift);
    material.userMaterialId |= encoded << frictionModeShift;
    shapeValue(backend_->setMaterial(material));
}

std::string Shape3D::getFrictionCombineMode() const {
    if (!isValid()) return "default";
    return combineModeName((shapeValue(backend_->material()).userMaterialId >> frictionModeShift) & combineModeMask);
}

void Shape3D::setRestitutionCombineMode(const std::string &mode) {
    const uint64_t encoded = parseCombineMode(mode, "Shape3D.setRestitutionCombineMode");
    if (!isValid()) return;
    SurfaceMaterial3D material = shapeValue(backend_->material());
    material.userMaterialId &= ~(combineModeMask << restitutionModeShift);
    material.userMaterialId |= encoded << restitutionModeShift;
    shapeValue(backend_->setMaterial(material));
}

std::string Shape3D::getRestitutionCombineMode() const {
    if (!isValid()) return "default";
    return combineModeName((shapeValue(backend_->material()).userMaterialId >> restitutionModeShift) & combineModeMask);
}

int Shape3D::getMaterialId() const {
    return isValid() ? static_cast<int>(static_cast<uint32_t>(shapeValue(backend_->material()).userMaterialId)) : 0;
}

void Shape3D::setExplosionScale(float scale) {
    if (!std::isfinite(scale) || scale < 0.f)
        throw eve::Exception("Shape3D.setExplosionScale: scale must be finite and >= 0");
    if (scale == explosionScale_) return;
    const float previous = explosionScale_;
    explosionScale_ = scale;
    try {
        if (body_ && body_->isValid()) recreate(isSensor());
    } catch (...) {
        explosionScale_ = previous;
        throw;
    }
}

void Shape3D::setDensity(float density) {
    if (!std::isfinite(density) || density < 0.f)
        throw eve::Exception("Shape3D.setDensity: density must be finite and >= 0");
    if (!isValid()) return;
    shapeValue(backend_->setDensity(density, true));
}

float Shape3D::getDensity() const { return isValid() ? shapeValue(backend_->density()) : 0.f; }

void Shape3D::setFilterBits(uint64_t categoryBits, uint64_t maskBits) {
    if (!isValid()) return;
    ShapeFilter3D filter = shapeValue(backend_->filter());
    filter.categoryBits = categoryBits;
    filter.maskBits     = maskBits;
    shapeValue(backend_->setFilter(filter, false));
}

uint64_t Shape3D::getCategoryBits() const { return isValid() ? shapeValue(backend_->filter()).categoryBits : 0; }

void Shape3D::setCategoryBits(uint64_t bits) {
    if (!isValid()) return;
    ShapeFilter3D filter = shapeValue(backend_->filter());
    filter.categoryBits = bits;
    shapeValue(backend_->setFilter(filter, true));
}

void Shape3D::setMaskBits(uint64_t bits) {
    if (!isValid()) return;
    ShapeFilter3D filter = shapeValue(backend_->filter());
    filter.maskBits = bits;
    shapeValue(backend_->setFilter(filter, true));
}

uint64_t Shape3D::getMaskBits() const { return isValid() ? shapeValue(backend_->filter()).maskBits : 0; }

void Shape3D::setGroupIndex(int index) {
    if (!isValid()) return;
    ShapeFilter3D filter = shapeValue(backend_->filter());
    filter.groupIndex = index;
    shapeValue(backend_->setFilter(filter, true));
}

int Shape3D::getGroupIndex() const { return isValid() ? shapeValue(backend_->filter()).groupIndex : 0; }

bool Shape3D::testPoint(float x, float y, float z) const {
    if (!isValid() || !body_ || !body_->isValid()) return false;
    return shapeValue(backend_->testPoint(x, y, z));
}

}  // namespace eve::physics
