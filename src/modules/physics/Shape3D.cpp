#include "physics/Shape3D.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include "common/Exception.h"

#include <box3d/box3d.h>

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

b3ShapeDef makeShapeDef(float density, float friction, float restitution, bool sensor,
                        bool hitEvents, bool preSolveEvents) {
    b3ShapeDef def                 = b3DefaultShapeDef();
    def.density                    = density;
    def.baseMaterial.friction      = friction;
    def.baseMaterial.restitution   = restitution;
    def.isSensor                   = sensor;
    def.enableContactEvents        = !sensor;
    def.enableSensorEvents         = true;
    def.enableHitEvents            = hitEvents && !sensor;
    def.enablePreSolveEvents       = preSolveEvents && !sensor;
    return def;
}

b3HullData *createCheckedHull(const std::vector<float> &vertices, int maxVertices,
                              const char *operation) {
    if (vertices.size() < 12 || vertices.size() % 3 != 0)
        throw eve::Exception("%s: vertices must contain at least four packed XYZ points",
                             operation);
    if (vertices.size() / 3 > 100000)
        throw eve::Exception("%s: source point count must be <= 100000", operation);
    if (maxVertices < 4 || maxVertices > 254)
        throw eve::Exception("%s: maxVertices must be in [4, 254]", operation);
    std::vector<b3Vec3> points;
    points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        if (!std::isfinite(vertices[i]) || !std::isfinite(vertices[i + 1]) ||
            !std::isfinite(vertices[i + 2]))
            throw eve::Exception("%s: all vertex components must be finite", operation);
        points.push_back({vertices[i], vertices[i + 1], vertices[i + 2]});
    }
    b3HullData *hull = b3CreateHull(points.data(), static_cast<int>(points.size()), maxVertices);
    if (!hull)
        throw eve::Exception("%s: points do not form a valid three-dimensional convex hull",
                             operation);
    return hull;
}

b3MeshData *createCheckedMesh(const std::vector<float> &vertices,
                              const std::vector<int32_t> &indices, bool weldVertices,
                              float weldTolerance, bool identifyEdges, bool useMedianSplit,
                              b3Transform transform,
                              const std::vector<uint8_t> *materialIndices,
                              const char *operation) {
    if (vertices.size() < 9 || vertices.size() % 3 != 0)
        throw eve::Exception("%s: vertices must contain at least three packed XYZ points",
                             operation);
    if (indices.size() < 3 || indices.size() % 3 != 0)
        throw eve::Exception("%s: indices must contain complete triangles", operation);
    if (vertices.size() / 3 > 1000000 || indices.size() / 3 > 2000000)
        throw eve::Exception("%s: mesh exceeds 1000000 vertices or 2000000 triangles",
                             operation);
    if (!std::isfinite(weldTolerance) || weldTolerance < 0.f)
        throw eve::Exception("%s: weldTolerance must be finite and >= 0", operation);
    std::vector<b3Vec3> points;
    points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        if (!std::isfinite(vertices[i]) || !std::isfinite(vertices[i + 1]) ||
            !std::isfinite(vertices[i + 2]))
            throw eve::Exception("%s: all vertex components must be finite", operation);
        const b3Vec3 point{vertices[i], vertices[i + 1], vertices[i + 2]};
        points.push_back(transform.p + b3RotateVector(transform.q, point));
    }
    for (int32_t index : indices) {
        if (index < 0 || static_cast<size_t>(index) >= points.size())
            throw eve::Exception("%s: triangle index is outside the vertex array", operation);
    }
    std::vector<int32_t> mutableIndices = indices;
    b3MeshDef def{};
    def.vertices = points.data();
    def.indices = mutableIndices.data();
    def.materialIndices = materialIndices && !materialIndices->empty()
                              ? const_cast<uint8_t *>(materialIndices->data())
                              : nullptr;
    def.vertexCount = static_cast<int>(points.size());
    def.triangleCount = static_cast<int>(indices.size() / 3);
    def.weldVertices = weldVertices;
    def.weldTolerance = weldTolerance;
    def.identifyEdges = identifyEdges;
    def.useMedianSplit = useMedianSplit;
    b3MeshData *mesh = b3CreateMesh(&def, nullptr, 0);
    if (!mesh || mesh->triangleCount != def.triangleCount) {
        if (mesh) b3DestroyMesh(mesh);
        throw eve::Exception("%s: mesh contains degenerate or zero-area triangles", operation);
    }
    return mesh;
}

b3HeightFieldData *createCheckedHeightField(int countX, int countZ, float cellSizeX,
                                            float cellSizeZ,
                                            const std::vector<float> &heights,
                                            float globalMin, float globalMax,
                                            bool clockwiseWinding, const char *operation) {
    if (countX < 2 || countZ < 2)
        throw eve::Exception("%s: countX and countZ must be >= 2", operation);
    const size_t sampleCount = static_cast<size_t>(countX) * static_cast<size_t>(countZ);
    if (sampleCount > 16000000)
        throw eve::Exception("%s: sample count must be <= 16000000", operation);
    if (heights.size() != sampleCount)
        throw eve::Exception("%s: heights size must equal countX * countZ", operation);
    if (!(cellSizeX > 0.f) || !(cellSizeZ > 0.f) || !std::isfinite(cellSizeX) ||
        !std::isfinite(cellSizeZ))
        throw eve::Exception("%s: cell sizes must be finite and > 0", operation);
    if (!std::isfinite(globalMin) || !std::isfinite(globalMax) || globalMin > globalMax)
        throw eve::Exception("%s: global height range must be finite and ordered", operation);
    for (float height : heights) {
        if (!std::isfinite(height) || height < globalMin || height > globalMax)
            throw eve::Exception("%s: every height must be finite and inside global range",
                                 operation);
    }
    std::vector<float> mutableHeights = heights;
    b3HeightFieldDef def{};
    def.heights = mutableHeights.data();
    def.scale = {cellSizeX, 1.f, cellSizeZ};
    def.countX = countX;
    def.countZ = countZ;
    def.globalMinimumHeight = globalMin;
    def.globalMaximumHeight = globalMax;
    def.clockwiseWinding = clockwiseWinding;
    return b3CreateHeightField(&def);
}

}  // namespace

Shape3D::Shape3D(World3D *world, Body3D *body, b3ShapeId shapeId, Kind kind, float a, float b,
                 float c, std::vector<float> hullVertices, int hullMaxVertices,
                 std::vector<float> meshVertices, std::vector<int32_t> meshIndices,
                 b3MeshData *meshData, bool meshWeldVertices, float meshWeldTolerance,
                 bool meshIdentifyEdges, bool meshUseMedianSplit,
                 std::vector<float> heightValues, int heightCountX, int heightCountZ,
                 float heightCellSizeX, float heightCellSizeZ, float heightGlobalMin,
                 float heightGlobalMax, bool heightClockwise, b3HeightFieldData *heightData)
    : world_(world), body_(body), shapeId_(shapeId), kind_(kind), a_(a), b_(b), c_(c),
      hullVertices_(std::move(hullVertices)), hullMaxVertices_(hullMaxVertices),
      meshVertices_(std::move(meshVertices)), meshIndices_(std::move(meshIndices)),
      meshData_(meshData), meshWeldVertices_(meshWeldVertices),
      meshWeldTolerance_(meshWeldTolerance), meshIdentifyEdges_(meshIdentifyEdges),
      meshUseMedianSplit_(meshUseMedianSplit),
      heightValues_(std::move(heightValues)), heightCountX_(heightCountX),
      heightCountZ_(heightCountZ), heightCellSizeX_(heightCellSizeX),
      heightCellSizeZ_(heightCellSizeZ), heightGlobalMin_(heightGlobalMin),
      heightGlobalMax_(heightGlobalMax), heightClockwise_(heightClockwise),
      heightData_(heightData),
      id_(world ? world->nextShapeId() : 0) {
    if (kind_ == Kind::TriangleMesh && isValid() && !meshMaterialsDirty_) {
        const int materialCount = b3Shape_GetMeshMaterialCount(shapeId_);
        meshMaterials_.reserve(static_cast<size_t>(materialCount));
        for (int i = 0; i < materialCount; ++i)
            meshMaterials_.push_back(b3Shape_GetMeshSurfaceMaterial(shapeId_, i));
    }
    if (world_) world_->registerShapeHandle(this);
}

Shape3D::~Shape3D() {
    if (isValid() && body_ && body_->isValid()) {
        b3Shape_SetUserData(shapeId_, nullptr);
        b3DestroyShape(shapeId_, true);
        if (world_) world_->forgetShape(this);
    }
    if (meshData_) {
        b3DestroyMesh(meshData_);
        meshData_ = nullptr;
    }
    if (heightData_) {
        b3DestroyHeightField(heightData_);
        heightData_ = nullptr;
    }
    shapeId_ = {};
    body_    = nullptr;
    world_   = nullptr;
}

bool Shape3D::isValid() const { return b3Shape_IsValid(shapeId_); }

void Shape3D::invalidate() {
    if (isValid()) b3Shape_SetUserData(shapeId_, nullptr);
    shapeId_ = {};
    body_    = nullptr;
    world_   = nullptr;
}

void Shape3D::destroy() {
    if (!isValid() || !body_ || !body_->isValid()) {
        invalidate();
        if (meshData_) {
            b3DestroyMesh(meshData_);
            meshData_ = nullptr;
        }
        if (heightData_) {
            b3DestroyHeightField(heightData_);
            heightData_ = nullptr;
        }
        return;
    }
    b3Shape_SetUserData(shapeId_, nullptr);
    b3DestroyShape(shapeId_, true);
    if (meshData_) {
        b3DestroyMesh(meshData_);
        meshData_ = nullptr;
    }
    if (heightData_) {
        b3DestroyHeightField(heightData_);
        heightData_ = nullptr;
    }
    if (world_) world_->forgetShape(this);
    shapeId_ = {};
    body_    = nullptr;
    world_   = nullptr;
}

void Shape3D::recreate(bool sensor) {
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D: cannot rebuild geometry after body destruction");

    float density = isValid() ? b3Shape_GetDensity(shapeId_) : 1.f;
    b3SurfaceMaterial material =
        isValid() ? b3Shape_GetSurfaceMaterial(shapeId_) : b3DefaultSurfaceMaterial();
    if (kind_ == Kind::TriangleMesh && isValid() && !meshMaterialsDirty_) {
        const int materialCount = b3Shape_GetMeshMaterialCount(shapeId_);
        meshMaterials_.clear();
        meshMaterials_.reserve(static_cast<size_t>(materialCount));
        for (int i = 0; i < materialCount; ++i)
            meshMaterials_.push_back(b3Shape_GetMeshSurfaceMaterial(shapeId_, i));
    }
    b3Filter filter = isValid() ? b3Shape_GetFilter(shapeId_) : b3DefaultShapeDef().filter;

    if (isValid()) {
        b3Shape_SetUserData(shapeId_, nullptr);
        b3DestroyShape(shapeId_, false);
        shapeId_ = {};
    }
    if (meshData_) {
        b3DestroyMesh(meshData_);
        meshData_ = nullptr;
    }
    if (heightData_) {
        b3DestroyHeightField(heightData_);
        heightData_ = nullptr;
    }

    b3ShapeDef def = makeShapeDef(density, material.friction, material.restitution, sensor,
                                  hitEventsEnabled_, oneWayEnabled_);
    def.enableCustomFiltering = contactOverrideFiltering_;
    def.explosionScale = explosionScale_;
    def.baseMaterial = material;
    def.filter = filter;
    const b3Quat localRotation{{localQx_, localQy_, localQz_}, localQw_};
    const b3Vec3 localPosition{localX_, localY_, localZ_};
    switch (kind_) {
        case Kind::Box: {
            b3BoxHull box = b3MakeBoxHull(a_, b_, c_);
            b3Transform transform{localPosition, localRotation};
            shapeId_ = b3CreateTransformedHullShape(body_->raw(), &def, &box.base, transform,
                                                     b3Vec3{1.f, 1.f, 1.f});
            break;
        }
        case Kind::Sphere: {
            b3Sphere sphere;
            sphere.center = localPosition;
            sphere.radius = a_;
            shapeId_      = b3CreateSphereShape(body_->raw(), &def, &sphere);
            break;
        }
        case Kind::Capsule: {
            b3Capsule capsule;
            const b3Vec3 axis = b3RotateVector(localRotation, b3Vec3{0.f, a_, 0.f});
            capsule.center1 = localPosition - axis;
            capsule.center2 = localPosition + axis;
            capsule.radius  = b_;
            shapeId_        = b3CreateCapsuleShape(body_->raw(), &def, &capsule);
            break;
        }
        case Kind::ConvexHull: {
            b3HullData *hull =
                createCheckedHull(hullVertices_, hullMaxVertices_, "Shape3D.recreate");
            b3Transform transform{localPosition, localRotation};
            shapeId_ = b3CreateTransformedHullShape(body_->raw(), &def, hull, transform,
                                                     b3Vec3{1.f, 1.f, 1.f});
            b3DestroyHull(hull);
            break;
        }
        case Kind::TriangleMesh: {
            if (b3Body_GetType(body_->raw()) != b3_staticBody)
                throw eve::Exception("Shape3D.recreate: triangle meshes require a static body");
            b3Transform transform{localPosition, localRotation};
            meshData_ = createCheckedMesh(meshVertices_, meshIndices_, meshWeldVertices_,
                                          meshWeldTolerance_, meshIdentifyEdges_,
                                          meshUseMedianSplit_, transform,
                                          &meshMaterialIndices_, "Shape3D.recreate");
            if (!meshMaterials_.empty()) {
                def.materials = meshMaterials_.data();
                def.materialCount = static_cast<int>(meshMaterials_.size());
            }
            shapeId_ = b3CreateMeshShape(body_->raw(), &def, meshData_, b3Vec3_one);
            break;
        }
        case Kind::HeightField: {
            if (b3Body_GetType(body_->raw()) != b3_staticBody)
                throw eve::Exception("Shape3D.recreate: height fields require a static body");
            heightData_ = createCheckedHeightField(
                heightCountX_, heightCountZ_, heightCellSizeX_, heightCellSizeZ_, heightValues_,
                heightGlobalMin_, heightGlobalMax_, heightClockwise_, "Shape3D.recreate");
            shapeId_ = b3CreateHeightFieldShape(body_->raw(), &def, heightData_);
            break;
        }
    }
    b3Shape_SetUserData(shapeId_, this);
    meshMaterialsDirty_ = false;
    if (world_) world_->registerShapeHandle(this);
}

void Shape3D::setSensor(bool sensor) {
    if (!body_ || !body_->isValid()) return;
    if (isValid() && b3Shape_IsSensor(shapeId_) == sensor) return;
    recreate(sensor);
}

void Shape3D::enableContactOverrideFiltering() {
    if (contactOverrideFiltering_) return;
    contactOverrideFiltering_ = true;
    recreate(isSensor());
}

void Shape3D::setTag(int tag) {
    tag_ = tag;
    if (world_) world_->updateShapeTag(this);
}

void Shape3D::setHitEventsEnabled(bool enabled) {
    hitEventsEnabled_ = enabled;
    if (!isValid()) return;
    b3Shape_EnableHitEvents(shapeId_, enabled && !isSensor());
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
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    oneWayLocalNormalX_ = nx * inverseLength;
    oneWayLocalNormalY_ = ny * inverseLength;
    oneWayLocalNormalZ_ = nz * inverseLength;
    oneWayPlaneOffset_ = planeOffset;
    oneWayMargin_ = margin;
    oneWayMinNormalDot_ = minNormalDot;
    oneWayEnabled_ = true;
    if (isValid()) b3Shape_EnablePreSolveEvents(shapeId_, !isSensor());
}

void Shape3D::disableOneWay() {
    oneWayEnabled_ = false;
    if (isValid()) b3Shape_EnablePreSolveEvents(shapeId_, false);
}

void Shape3D::refreshOneWayWorldData() {
    if (!oneWayEnabled_ || !body_ || !body_->isValid()) return;
    const b3WorldTransform bodyTransform = b3Body_GetTransform(body_->raw());
    const b3Quat localRotation{{localQx_, localQy_, localQz_}, localQw_};
    const b3Quat worldRotation = b3MulQuat(bodyTransform.q, localRotation);
    const b3Vec3 localNormal{oneWayLocalNormalX_, oneWayLocalNormalY_, oneWayLocalNormalZ_};
    const b3Vec3 worldNormal = b3RotateVector(worldRotation, localNormal);
    const b3Vec3 localPosition{localX_, localY_, localZ_};
    const b3Vec3 bodyOffset = b3RotateVector(bodyTransform.q, localPosition);
    const b3Pos shapeOrigin = b3OffsetPos(bodyTransform.p, bodyOffset);
    const b3Pos planePoint = b3OffsetPos(shapeOrigin, oneWayPlaneOffset_ * worldNormal);
    oneWayWorldPointX_ = planePoint.x;
    oneWayWorldPointY_ = planePoint.y;
    oneWayWorldPointZ_ = planePoint.z;
    oneWayWorldNormalX_ = worldNormal.x;
    oneWayWorldNormalY_ = worldNormal.y;
    oneWayWorldNormalZ_ = worldNormal.z;
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
    switch (kind_) {
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
    if (kind_ != Kind::Box)
        throw eve::Exception("Shape3D.setBoxSize: shape is not a box");
    if (!(width > 0.f) || !(height > 0.f) || !(depth > 0.f) || !std::isfinite(width) ||
        !std::isfinite(height) || !std::isfinite(depth))
        throw eve::Exception("Shape3D.setBoxSize: dimensions must be finite and > 0");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setBoxSize: body destroyed");
    const float halfWidth = 0.5f * width;
    const float halfHeight = 0.5f * height;
    const float halfDepth = 0.5f * depth;
    if (a_ == halfWidth && b_ == halfHeight && c_ == halfDepth) return;
    const bool sensor = isSensor();
    a_ = halfWidth;
    b_ = halfHeight;
    c_ = halfDepth;
    recreate(sensor);
}

void Shape3D::setSphereRadius(float radius) {
    if (kind_ != Kind::Sphere)
        throw eve::Exception("Shape3D.setSphereRadius: shape is not a sphere");
    if (!(radius > 0.f) || !std::isfinite(radius))
        throw eve::Exception("Shape3D.setSphereRadius: radius must be finite and > 0");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setSphereRadius: body destroyed");
    if (a_ == radius) return;
    const bool sensor = isSensor();
    a_ = radius;
    recreate(sensor);
}

void Shape3D::setCapsuleSize(float height, float radius) {
    if (kind_ != Kind::Capsule)
        throw eve::Exception("Shape3D.setCapsuleSize: shape is not a capsule");
    if (!(height >= 0.f) || !(radius > 0.f) || !std::isfinite(height) ||
        !std::isfinite(radius))
        throw eve::Exception(
            "Shape3D.setCapsuleSize: height must be finite and >= 0; radius finite and > 0");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setCapsuleSize: body destroyed");
    const float halfHeight = 0.5f * height;
    if (a_ == halfHeight && b_ == radius) return;
    const bool sensor = isSensor();
    a_ = halfHeight;
    b_ = radius;
    recreate(sensor);
}

void Shape3D::setConvexHullVertices(const std::vector<float> &vertices, int maxVertices) {
    if (kind_ != Kind::ConvexHull)
        throw eve::Exception("Shape3D.setConvexHullVertices: shape is not a convex hull");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setConvexHullVertices: body destroyed");
    b3HullData *validated =
        createCheckedHull(vertices, maxVertices, "Shape3D.setConvexHullVertices");
    b3DestroyHull(validated);
    if (vertices == hullVertices_ && maxVertices == hullMaxVertices_) return;
    const bool sensor = isSensor();
    hullVertices_ = vertices;
    hullMaxVertices_ = maxVertices;
    recreate(sensor);
}

void Shape3D::setTriangleMeshData(const std::vector<float> &vertices,
                                  const std::vector<int32_t> &indices, bool weldVertices,
                                  float weldTolerance, bool identifyEdges,
                                  bool useMedianSplit) {
    if (kind_ != Kind::TriangleMesh)
        throw eve::Exception("Shape3D.setTriangleMeshData: shape is not a triangle mesh");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setTriangleMeshData: body destroyed");
    if (b3Body_GetType(body_->raw()) != b3_staticBody)
        throw eve::Exception("Shape3D.setTriangleMeshData: triangle meshes require a static body");
    const b3Transform transform{{localX_, localY_, localZ_},
                                b3Quat{{localQx_, localQy_, localQz_}, localQw_}};
    b3MeshData *validated = createCheckedMesh(vertices, indices, weldVertices, weldTolerance,
                                              identifyEdges, useMedianSplit, transform,
                                              nullptr,
                                              "Shape3D.setTriangleMeshData");
    b3DestroyMesh(validated);
    if (vertices == meshVertices_ && indices == meshIndices_ &&
        weldVertices == meshWeldVertices_ && weldTolerance == meshWeldTolerance_ &&
        identifyEdges == meshIdentifyEdges_ && useMedianSplit == meshUseMedianSplit_)
        return;
    const bool sensor = isSensor();
    meshVertices_ = vertices;
    meshIndices_ = indices;
    meshWeldVertices_ = weldVertices;
    meshWeldTolerance_ = weldTolerance;
    meshIdentifyEdges_ = identifyEdges;
    meshUseMedianSplit_ = useMedianSplit;
    if (meshMaterialIndices_.size() != indices.size() / 3) {
        meshMaterialIndices_.clear();
        if (meshMaterials_.size() > 1) meshMaterials_.resize(1);
    }
    recreate(sensor);
}

void Shape3D::setTriangleMeshMaterialIndices(
    const std::vector<int32_t> &materialIndices) {
    if (kind_ != Kind::TriangleMesh)
        throw eve::Exception(
            "Shape3D.setTriangleMeshMaterialIndices: shape is not a triangle mesh");
    const size_t triangleCount = meshIndices_.size() / 3;
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
    if (encoded == meshMaterialIndices_ &&
        requiredCount == static_cast<int>(meshMaterials_.size()))
        return;
    const bool sensor = isSensor();
    const auto oldIndices = meshMaterialIndices_;
    const auto oldMaterials = meshMaterials_;
    meshMaterialIndices_ = std::move(encoded);
    if (meshMaterials_.empty()) meshMaterials_.push_back(b3DefaultSurfaceMaterial());
    meshMaterials_.resize(static_cast<size_t>(requiredCount), meshMaterials_.front());
    meshMaterialsDirty_ = true;
    try {
        recreate(sensor);
    } catch (...) {
        meshMaterialIndices_ = oldIndices;
        meshMaterials_ = oldMaterials;
        meshMaterialsDirty_ = true;
        recreate(sensor);
        throw;
    }
}

int Shape3D::getTriangleMeshMaterialIndex(int triangleIndex) const {
    if (kind_ != Kind::TriangleMesh)
        throw eve::Exception(
            "Shape3D.getTriangleMeshMaterialIndex: shape is not a triangle mesh");
    const int triangleCount = static_cast<int>(meshIndices_.size() / 3);
    if (triangleIndex < 0 || triangleIndex >= triangleCount)
        throw eve::Exception("Shape3D.getTriangleMeshMaterialIndex: triangle out of range");
    return meshMaterialIndices_.empty() ? 0 : meshMaterialIndices_[static_cast<size_t>(triangleIndex)];
}

int Shape3D::getTriangleMeshMaterialCount() const {
    return kind_ == Kind::TriangleMesh ? std::max(1, static_cast<int>(meshMaterials_.size())) : 0;
}

void Shape3D::setTriangleMeshMaterial(int slot, float friction, float restitution,
                                      float rollingResistance, float tangentX,
                                      float tangentY, float tangentZ, int materialId,
                                      const std::string &frictionMode,
                                      const std::string &restitutionMode) {
    if (kind_ != Kind::TriangleMesh)
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
    b3SurfaceMaterial material = b3DefaultSurfaceMaterial();
    material.friction = friction;
    material.restitution = restitution;
    material.rollingResistance = rollingResistance;
    material.tangentVelocity = {tangentX, tangentY, tangentZ};
    material.userMaterialId = static_cast<uint32_t>(materialId) |
                              (frictionCode << frictionModeShift) |
                              (restitutionCode << restitutionModeShift);
    if (meshMaterials_.empty()) meshMaterials_.push_back(b3DefaultSurfaceMaterial());
    meshMaterials_[static_cast<size_t>(slot)] = material;
    if (isValid()) b3Shape_SetMeshMaterial(shapeId_, material, slot);
}

float Shape3D::getTriangleMeshMaterialFriction(int slot) const {
    if (kind_ != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.getTriangleMeshMaterialFriction: slot out of range");
    return meshMaterials_.empty() ? getFriction() : meshMaterials_[static_cast<size_t>(slot)].friction;
}

float Shape3D::getTriangleMeshMaterialRestitution(int slot) const {
    if (kind_ != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.getTriangleMeshMaterialRestitution: slot out of range");
    return meshMaterials_.empty() ? getRestitution() : meshMaterials_[static_cast<size_t>(slot)].restitution;
}

float Shape3D::getTriangleMeshMaterialRollingResistance(int slot) const {
    if (kind_ != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception(
            "Shape3D.getTriangleMeshMaterialRollingResistance: slot out of range");
    return meshMaterials_.empty() ? getRollingResistance()
                                  : meshMaterials_[static_cast<size_t>(slot)].rollingResistance;
}

int Shape3D::getTriangleMeshMaterialId(int slot) const {
    if (kind_ != Kind::TriangleMesh || slot < 0 || slot >= getTriangleMeshMaterialCount())
        throw eve::Exception("Shape3D.getTriangleMeshMaterialId: slot out of range");
    const uint64_t id = meshMaterials_.empty()
                            ? b3Shape_GetSurfaceMaterial(shapeId_).userMaterialId
                            : meshMaterials_[static_cast<size_t>(slot)].userMaterialId;
    return static_cast<int>(static_cast<uint32_t>(id));
}

void Shape3D::setHeightFieldHeights(const std::vector<float> &heights) {
    if (kind_ != Kind::HeightField)
        throw eve::Exception("Shape3D.setHeightFieldHeights: shape is not a height field");
    if (!body_ || !body_->isValid())
        throw eve::Exception("Shape3D.setHeightFieldHeights: body destroyed");
    b3HeightFieldData *validated = createCheckedHeightField(
        heightCountX_, heightCountZ_, heightCellSizeX_, heightCellSizeZ_, heights,
        heightGlobalMin_, heightGlobalMax_, heightClockwise_,
        "Shape3D.setHeightFieldHeights");
    b3DestroyHeightField(validated);
    if (heights == heightValues_) return;
    const bool sensor = isSensor();
    heightValues_ = heights;
    recreate(sensor);
}

void Shape3D::setHeightFieldRegion(int x, int z, int width, int depth,
                                   const std::vector<float> &heights) {
    if (kind_ != Kind::HeightField)
        throw eve::Exception("Shape3D.setHeightFieldRegion: shape is not a height field");
    if (x < 0 || z < 0 || width < 1 || depth < 1 || x > heightCountX_ - width ||
        z > heightCountZ_ - depth)
        throw eve::Exception("Shape3D.setHeightFieldRegion: region is outside the sample grid");
    if (heights.size() != static_cast<size_t>(width) * static_cast<size_t>(depth))
        throw eve::Exception("Shape3D.setHeightFieldRegion: data size must equal width * depth");
    std::vector<float> updated = heightValues_;
    for (int row = 0; row < depth; ++row) {
        for (int column = 0; column < width; ++column) {
            updated[static_cast<size_t>(z + row) * static_cast<size_t>(heightCountX_) +
                    static_cast<size_t>(x + column)] =
                heights[static_cast<size_t>(row) * static_cast<size_t>(width) +
                        static_cast<size_t>(column)];
        }
    }
    setHeightFieldHeights(updated);
}

float Shape3D::getHeightFieldHeight(int x, int z) const {
    if (kind_ != Kind::HeightField)
        throw eve::Exception("Shape3D.getHeightFieldHeight: shape is not a height field");
    if (x < 0 || x >= heightCountX_ || z < 0 || z >= heightCountZ_)
        throw eve::Exception("Shape3D.getHeightFieldHeight: sample index out of range");
    return heightValues_[static_cast<size_t>(z) * static_cast<size_t>(heightCountX_) +
                         static_cast<size_t>(x)];
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
    if (kind_ == Kind::HeightField &&
        (px != 0.f || py != 0.f || pz != 0.f || normalizedQx != 0.f ||
         normalizedQy != 0.f || normalizedQz != 0.f || normalizedQw != 1.f))
        throw eve::Exception(
            "Shape3D.setLocalTransform: height fields use their Body transform and require an identity local transform");
    if (localX_ == px && localY_ == py && localZ_ == pz && localQx_ == normalizedQx &&
        localQy_ == normalizedQy && localQz_ == normalizedQz && localQw_ == normalizedQw)
        return;

    const bool sensor = isSensor();
    localX_ = px;
    localY_ = py;
    localZ_ = pz;
    localQx_ = normalizedQx;
    localQy_ = normalizedQy;
    localQz_ = normalizedQz;
    localQw_ = normalizedQw;
    recreate(sensor);
}

void Shape3D::setLocalPosition(float x, float y, float z) {
    setLocalTransform(x, y, z, localQx_, localQy_, localQz_, localQw_);
}

void Shape3D::setLocalRotation(float qx, float qy, float qz, float qw) {
    setLocalTransform(localX_, localY_, localZ_, qx, qy, qz, qw);
}

bool Shape3D::isSensor() const { return isValid() ? b3Shape_IsSensor(shapeId_) : false; }

void Shape3D::setFriction(float friction) {
    if (!std::isfinite(friction) || friction < 0.f)
        throw eve::Exception("Shape3D.setFriction: friction must be finite and >= 0");
    if (!isValid()) return;
    b3Shape_SetFriction(shapeId_, friction);
}

float Shape3D::getFriction() const { return isValid() ? b3Shape_GetFriction(shapeId_) : 0.f; }

void Shape3D::setRestitution(float restitution) {
    if (!std::isfinite(restitution) || restitution < 0.f)
        throw eve::Exception("Shape3D.setRestitution: restitution must be finite and >= 0");
    if (!isValid()) return;
    b3Shape_SetRestitution(shapeId_, restitution);
}

float Shape3D::getRestitution() const {
    return isValid() ? b3Shape_GetRestitution(shapeId_) : 0.f;
}

void Shape3D::setRollingResistance(float resistance) {
    if (!std::isfinite(resistance) || resistance < 0.f)
        throw eve::Exception(
            "Shape3D.setRollingResistance: resistance must be finite and >= 0");
    if (!isValid()) return;
    b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(shapeId_);
    material.rollingResistance = resistance;
    b3Shape_SetSurfaceMaterial(shapeId_, material);
}

float Shape3D::getRollingResistance() const {
    return isValid() ? b3Shape_GetSurfaceMaterial(shapeId_).rollingResistance : 0.f;
}

void Shape3D::setTangentVelocity(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("Shape3D.setTangentVelocity: components must be finite");
    if (!isValid()) return;
    b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(shapeId_);
    material.tangentVelocity = b3Vec3{x, y, z};
    b3Shape_SetSurfaceMaterial(shapeId_, material);
}

float Shape3D::getTangentVelocityX() const {
    return isValid() ? b3Shape_GetSurfaceMaterial(shapeId_).tangentVelocity.x : 0.f;
}

float Shape3D::getTangentVelocityY() const {
    return isValid() ? b3Shape_GetSurfaceMaterial(shapeId_).tangentVelocity.y : 0.f;
}

float Shape3D::getTangentVelocityZ() const {
    return isValid() ? b3Shape_GetSurfaceMaterial(shapeId_).tangentVelocity.z : 0.f;
}

void Shape3D::setMaterialId(int id) {
    if (!isValid()) return;
    b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(shapeId_);
    material.userMaterialId =
        (material.userMaterialId & ~materialIdMask) | static_cast<uint32_t>(id);
    b3Shape_SetSurfaceMaterial(shapeId_, material);
}

void Shape3D::setFrictionCombineMode(const std::string &mode) {
    const uint64_t encoded = parseCombineMode(mode, "Shape3D.setFrictionCombineMode");
    if (!isValid()) return;
    b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(shapeId_);
    material.userMaterialId &= ~(combineModeMask << frictionModeShift);
    material.userMaterialId |= encoded << frictionModeShift;
    b3Shape_SetSurfaceMaterial(shapeId_, material);
}

std::string Shape3D::getFrictionCombineMode() const {
    if (!isValid()) return "default";
    return combineModeName((b3Shape_GetSurfaceMaterial(shapeId_).userMaterialId >>
                            frictionModeShift) & combineModeMask);
}

void Shape3D::setRestitutionCombineMode(const std::string &mode) {
    const uint64_t encoded = parseCombineMode(mode, "Shape3D.setRestitutionCombineMode");
    if (!isValid()) return;
    b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(shapeId_);
    material.userMaterialId &= ~(combineModeMask << restitutionModeShift);
    material.userMaterialId |= encoded << restitutionModeShift;
    b3Shape_SetSurfaceMaterial(shapeId_, material);
}

std::string Shape3D::getRestitutionCombineMode() const {
    if (!isValid()) return "default";
    return combineModeName((b3Shape_GetSurfaceMaterial(shapeId_).userMaterialId >>
                            restitutionModeShift) & combineModeMask);
}

int Shape3D::getMaterialId() const {
    return isValid()
               ? static_cast<int>(static_cast<uint32_t>(
                     b3Shape_GetSurfaceMaterial(shapeId_).userMaterialId))
               : 0;
}

void Shape3D::setExplosionScale(float scale) {
    if (!std::isfinite(scale) || scale < 0.f)
        throw eve::Exception("Shape3D.setExplosionScale: scale must be finite and >= 0");
    if (scale == explosionScale_) return;
    explosionScale_ = scale;
    if (body_ && body_->isValid()) recreate(isSensor());
}

void Shape3D::setDensity(float density) {
    if (!std::isfinite(density) || density < 0.f)
        throw eve::Exception("Shape3D.setDensity: density must be finite and >= 0");
    if (!isValid()) return;
    b3Shape_SetDensity(shapeId_, density, true);
}

float Shape3D::getDensity() const { return isValid() ? b3Shape_GetDensity(shapeId_) : 0.f; }

void Shape3D::setFilterBits(uint64_t categoryBits, uint64_t maskBits) {
    if (!isValid()) return;
    b3Filter filter     = b3Shape_GetFilter(shapeId_);
    filter.categoryBits = categoryBits;
    filter.maskBits     = maskBits;
    b3Shape_SetFilter(shapeId_, filter, false);
}

uint64_t Shape3D::getCategoryBits() const {
    return isValid() ? b3Shape_GetFilter(shapeId_).categoryBits : 0;
}

void Shape3D::setCategoryBits(uint64_t bits) {
    if (!isValid()) return;
    b3Filter filter = b3Shape_GetFilter(shapeId_);
    filter.categoryBits = bits;
    b3Shape_SetFilter(shapeId_, filter, true);
}

void Shape3D::setMaskBits(uint64_t bits) {
    if (!isValid()) return;
    b3Filter filter = b3Shape_GetFilter(shapeId_);
    filter.maskBits = bits;
    b3Shape_SetFilter(shapeId_, filter, true);
}

uint64_t Shape3D::getMaskBits() const {
    return isValid() ? b3Shape_GetFilter(shapeId_).maskBits : 0;
}

void Shape3D::setGroupIndex(int index) {
    if (!isValid()) return;
    b3Filter filter = b3Shape_GetFilter(shapeId_);
    filter.groupIndex = index;
    b3Shape_SetFilter(shapeId_, filter, true);
}

int Shape3D::getGroupIndex() const {
    return isValid() ? b3Shape_GetFilter(shapeId_).groupIndex : 0;
}

bool Shape3D::testPoint(float x, float y, float z) const {
    if (!isValid() || !body_ || !body_->isValid()) return false;

    b3Vec3 point{x, y, z};
    b3ShapeProxy proxy;
    proxy.points = &point;
    proxy.count  = 1;
    proxy.radius = 0.f;

    b3WorldTransform wt = b3Body_GetTransform(body_->raw());
    b3Transform xf;
    xf.p = b3Vec3{static_cast<float>(wt.p.x), static_cast<float>(wt.p.y),
                  static_cast<float>(wt.p.z)};
    xf.q = wt.q;

    switch (kind_) {
        case Kind::Box:
        case Kind::ConvexHull: {
            const b3HullData *hull = b3Shape_GetHull(shapeId_);
            if (!hull) return false;
            return b3OverlapHull(hull, xf, &proxy);
        }
        case Kind::Sphere: {
            b3Sphere sphere = b3Shape_GetSphere(shapeId_);
            return b3OverlapSphere(&sphere, xf, &proxy);
        }
        case Kind::Capsule: {
            b3Capsule capsule = b3Shape_GetCapsule(shapeId_);
            return b3OverlapCapsule(&capsule, xf, &proxy);
        }
        case Kind::TriangleMesh: {
            const b3Mesh mesh = b3Shape_GetMesh(shapeId_);
            return mesh.data ? b3OverlapMesh(&mesh, xf, &proxy) : false;
        }
        case Kind::HeightField: {
            const b3HeightFieldData *heightField = b3Shape_GetHeightField(shapeId_);
            return heightField ? b3OverlapHeightField(heightField, xf, &proxy) : false;
        }
    }
    return false;
}

}  // namespace eve::physics
