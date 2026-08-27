#pragma once

#include "physics/PhysicsHandles.h"

#include <box3d/id.h>
#include <box3d/types.h>

#include <string>
#include <vector>

#include <cstdint>

namespace eve::physics {

class Body3D;
class World3D;

/**
 * @brief 3D shape (box/sphere/capsule) attached to a Body3D with material
 * settings. Created via Body3D::new*Shape; owned by the world.
 */
class Shape3D {
public:
    /** @brief Shape geometry kind. */
    enum class Kind { Box, Sphere, Capsule, ConvexHull, TriangleMesh, HeightField };

    /** @brief Internal: wraps a Box3D shape (use Body3D::new*Shape). */
    Shape3D(World3D *world, Body3D *body, b3ShapeId shapeId, PhysicsShapeHandle runtimeHandle, Kind kind, float a,
            float b, float c, std::vector<float> hullVertices = {}, int hullMaxVertices = 64,
            std::vector<float> meshVertices = {}, std::vector<int32_t> meshIndices = {}, b3MeshData *meshData = nullptr,
            bool meshWeldVertices = true, float meshWeldTolerance = 0.001f, bool meshIdentifyEdges = true,
            bool meshUseMedianSplit = false, std::vector<float> heightValues = {}, int heightCountX = 0,
            int heightCountZ = 0, float heightCellSizeX = 1.f, float heightCellSizeZ = 1.f, float heightGlobalMin = 0.f,
            float heightGlobalMax = 0.f, bool heightClockwise = false, b3HeightFieldData *heightData = nullptr);
    ~Shape3D();

    Shape3D(const Shape3D &)            = delete;
    Shape3D &operator=(const Shape3D &) = delete;

    /** @brief Stable world-local id, preserved when sensor state recreates the backend shape. */
    int getId() const { return id_; }
    /** @brief Process-local generation-qualified handle owned by World3D. */
    [[nodiscard]] PhysicsShapeHandle runtimeHandle() const noexcept { return runtimeHandle_; }
    /** @brief User-defined integer tag preserved for the wrapper lifetime. */
    void setTag(int tag);
    /** @brief User-defined integer tag, zero by default. */
    int getTag() const { return tag_; }

    /** @brief Enables significant collision hit events for this shape. */
    void setHitEventsEnabled(bool enabled);
    /** @brief Whether significant collision hit events are enabled. */
    bool areHitEventsEnabled() const;

    /** @brief Kind name including primitives, "convexHull", "triangleMesh", or "heightField". */
    std::string getKind() const;
    /** @brief Atomically changes full box dimensions in meters. @throws eve::Exception on wrong kind or invalid size. */
    void setBoxSize(float width, float height, float depth);
    /** @brief Atomically changes sphere radius in meters. @throws eve::Exception on wrong kind or invalid radius. */
    void setSphereRadius(float radius);
    /**
     * @brief Atomically changes capsule segment height and radius in meters.
     * @param height Distance between hemisphere centers; may be zero.
     * @param radius Positive hemisphere radius.
     * @throws eve::Exception on wrong kind or invalid dimensions.
     */
    void setCapsuleSize(float height, float radius);
    /**
     * @brief Atomically rebuilds a convex hull from packed local XYZ vertices.
     * @param vertices At least four finite, non-coplanar points.
     * @param maxVertices Hull simplification budget in [4, 254].
     * @throws eve::Exception on another shape kind, malformed, or degenerate input.
     */
    void setConvexHullVertices(const std::vector<float> &vertices, int maxVertices = 64);
    /** @brief Source point count for a convex hull, or zero for another kind. */
    int getConvexHullPointCount() const {
        return kind_ == Kind::ConvexHull ? static_cast<int>(hullVertices_.size() / 3) : 0;
    }
    /** @brief Current convex-hull simplification budget, or zero for another kind. */
    int getConvexHullMaxVertices() const {
        return kind_ == Kind::ConvexHull ? hullMaxVertices_ : 0;
    }
    /**
     * @brief Atomically replaces static triangle-mesh source data and rebuilds its BVH.
     * @throws eve::Exception on another kind, invalid data, or non-static Body.
     */
    void setTriangleMeshData(const std::vector<float> &vertices,
                             const std::vector<int32_t> &indices, bool weldVertices = true,
                             float weldTolerance = 0.001f, bool identifyEdges = true,
                             bool useMedianSplit = false);
    /** @brief Source vertex count for a triangle mesh, or zero. */
    int getTriangleMeshVertexCount() const {
        return kind_ == Kind::TriangleMesh ? static_cast<int>(meshVertices_.size() / 3) : 0;
    }
    /** @brief Source triangle count for a triangle mesh, or zero. */
    int getTriangleMeshTriangleCount() const {
        return kind_ == Kind::TriangleMesh ? static_cast<int>(meshIndices_.size() / 3) : 0;
    }
    /** @brief Atomically assigns one material-slot index per source mesh triangle. */
    void setTriangleMeshMaterialIndices(const std::vector<int32_t> &materialIndices);
    /** @brief Material-slot index assigned to a source triangle. */
    int getTriangleMeshMaterialIndex(int triangleIndex) const;
    /** @brief Number of material slots on a triangle mesh, otherwise zero. */
    int getTriangleMeshMaterialCount() const;
    /**
     * @brief Replaces one triangle-mesh material slot without rebuilding geometry.
     * @param slot Material slot in [0, materialCount).
     * @param friction Finite non-negative Coulomb friction.
     * @param restitution Finite non-negative restitution.
     * @param rollingResistance Finite non-negative rolling resistance.
     * @param tangentX Local conveyor velocity X.
     * @param tangentY Local conveyor velocity Y.
     * @param tangentZ Local conveyor velocity Z.
     * @param materialId Low-32-bit application material identifier.
     * @param frictionMode Combine mode name.
     * @param restitutionMode Combine mode name.
     */
    void setTriangleMeshMaterial(int slot, float friction, float restitution,
                                 float rollingResistance, float tangentX, float tangentY,
                                 float tangentZ, int materialId,
                                 const std::string &frictionMode,
                                 const std::string &restitutionMode);
    /** @brief Friction of a triangle-mesh material slot. */
    float getTriangleMeshMaterialFriction(int slot) const;
    /** @brief Restitution of a triangle-mesh material slot. */
    float getTriangleMeshMaterialRestitution(int slot) const;
    /** @brief Rolling resistance of a triangle-mesh material slot. */
    float getTriangleMeshMaterialRollingResistance(int slot) const;
    /** @brief Application material ID of a triangle-mesh material slot. */
    int getTriangleMeshMaterialId(int slot) const;
    /** @brief Atomically replaces every source height and rebuilds compressed terrain data. */
    void setHeightFieldHeights(const std::vector<float> &heights);
    /** @brief Atomically updates a rectangular sample region in row-major order. */
    void setHeightFieldRegion(int x, int z, int width, int depth,
                              const std::vector<float> &heights);
    /** @brief Source height at a grid sample. */
    float getHeightFieldHeight(int x, int z) const;
    /** @brief Height-field sample count along local X, or zero. */
    int getHeightFieldCountX() const { return kind_ == Kind::HeightField ? heightCountX_ : 0; }
    /** @brief Height-field sample count along local Z, or zero. */
    int getHeightFieldCountZ() const { return kind_ == Kind::HeightField ? heightCountZ_ : 0; }
    /** @brief Height-field cell spacing along local X, or zero. */
    float getHeightFieldCellSizeX() const {
        return kind_ == Kind::HeightField ? heightCellSizeX_ : 0.f;
    }
    /** @brief Height-field cell spacing along local Z, or zero. */
    float getHeightFieldCellSizeZ() const {
        return kind_ == Kind::HeightField ? heightCellSizeZ_ : 0.f;
    }
    /** @brief Height-field shared quantization minimum, or zero. */
    float getHeightFieldGlobalMin() const {
        return kind_ == Kind::HeightField ? heightGlobalMin_ : 0.f;
    }
    /** @brief Height-field shared quantization maximum, or zero. */
    float getHeightFieldGlobalMax() const {
        return kind_ == Kind::HeightField ? heightGlobalMax_ : 0.f;
    }
    /** @brief Full box width, or zero for another geometry kind. */
    float getBoxWidth() const { return kind_ == Kind::Box ? 2.f * a_ : 0.f; }
    /** @brief Full box height, or zero for another geometry kind. */
    float getBoxHeight() const { return kind_ == Kind::Box ? 2.f * b_ : 0.f; }
    /** @brief Full box depth, or zero for another geometry kind. */
    float getBoxDepth() const { return kind_ == Kind::Box ? 2.f * c_ : 0.f; }
    /** @brief Sphere/capsule radius, or zero for a box. */
    float getRadius() const {
        return kind_ == Kind::Sphere ? a_ : (kind_ == Kind::Capsule ? b_ : 0.f);
    }
    /** @brief Capsule hemisphere-center distance, or zero for another kind. */
    float getCapsuleHeight() const { return kind_ == Kind::Capsule ? 2.f * a_ : 0.f; }

    /**
     * @brief Enables a one-way collision plane in shape-local space.
     * @param nx Local allowed-side normal X; normalized internally.
     * @param ny Local allowed-side normal Y.
     * @param nz Local allowed-side normal Z.
     * @param planeOffset Plane distance from the shape origin along the normal, in meters.
     * @param margin Allowed-side positional tolerance in meters, finite and non-negative.
     * @param minNormalDot Minimum contact/outward-normal dot in [-1,1].
     */
    void setOneWay(float nx, float ny, float nz, float planeOffset, float margin,
                   float minNormalDot);
    /** @brief Disables one-way filtering and restores ordinary two-sided collision. */
    void disableOneWay();
    /** @brief Whether one-way pre-solve filtering is requested. */
    bool isOneWay() const { return oneWayEnabled_; }

    /**
     * @brief Atomically sets the shape transform relative to its Body3D.
     * @param px Local translation X in meters.
     * @param py Local translation Y in meters.
     * @param pz Local translation Z in meters.
     * @param qx Quaternion X component, normalized internally.
     * @param qy Quaternion Y component.
     * @param qz Quaternion Z component.
     * @param qw Quaternion scalar component.
     */
    void setLocalTransform(float px, float py, float pz, float qx, float qy, float qz, float qw);
    /** @brief Sets local translation while preserving local rotation. */
    void setLocalPosition(float x, float y, float z);
    /** @brief Sets local rotation while preserving local translation. */
    void setLocalRotation(float qx, float qy, float qz, float qw);
    /** @brief Local translation X in meters. */
    float getLocalX() const { return localX_; }
    /** @brief Local translation Y in meters. */
    float getLocalY() const { return localY_; }
    /** @brief Local translation Z in meters. */
    float getLocalZ() const { return localZ_; }
    /** @brief Normalized local quaternion X. */
    float getLocalRotX() const { return localQx_; }
    /** @brief Normalized local quaternion Y. */
    float getLocalRotY() const { return localQy_; }
    /** @brief Normalized local quaternion Z. */
    float getLocalRotZ() const { return localQz_; }
    /** @brief Normalized local quaternion scalar component. */
    float getLocalRotW() const { return localQw_; }

    /** @brief Sensor shapes report contacts but never collide. */
    void setSensor(bool sensor);
    bool isSensor() const;

    /** @brief Material properties. */
    void  setFriction(float friction);
    float getFriction() const;

    void  setRestitution(float restitution);
    float getRestitution() const;

    /** @brief Rolling resistance coefficient, finite and non-negative. */
    void setRollingResistance(float resistance);
    /** @brief Current rolling resistance; physically affects spheres and capsules. */
    float getRollingResistance() const;
    /** @brief Sets local-space conveyor velocity projected onto contact surfaces. */
    void setTangentVelocity(float x, float y, float z);
    /** @brief Local-space conveyor velocity X component. */
    float getTangentVelocityX() const;
    /** @brief Local-space conveyor velocity Y component. */
    float getTangentVelocityY() const;
    /** @brief Local-space conveyor velocity Z component. */
    float getTangentVelocityZ() const;
    /** @brief Sets a low-32-bit application material identifier exposed by native queries. */
    void setMaterialId(int id);
    /** @brief Low-32-bit application material identifier. */
    int getMaterialId() const;
    /** @brief Sets non-negative multiplier applied to geometry-aware explosion impulses. */
    void setExplosionScale(float scale);
    /** @brief Current explosion impulse multiplier; zero opts this shape out. */
    float getExplosionScale() const { return explosionScale_; }
    /** @brief Sets friction combine mode: default, average, minimum, multiply, or maximum. */
    void setFrictionCombineMode(const std::string &mode);
    /** @brief Current friction combine mode. */
    std::string getFrictionCombineMode() const;
    /** @brief Sets restitution combine mode: default, average, minimum, multiply, or maximum. */
    void setRestitutionCombineMode(const std::string &mode);
    /** @brief Current restitution combine mode. */
    std::string getRestitutionCombineMode() const;

    void  setDensity(float density);
    float getDensity() const;

    /** @brief Collision filter bits used by world ray/query filters (Box3D b3Filter).
     * categoryBits 声明本形状属于哪些类别；maskBits 声明本形状接受哪些类别碰撞。 */
    void     setFilterBits(uint64_t categoryBits, uint64_t maskBits);
    uint64_t getCategoryBits() const;
    /** @brief Sets collision category bits without changing the mask. */
    void setCategoryBits(uint64_t bits);
    /** @brief Sets collision mask bits without changing the category. */
    void setMaskBits(uint64_t bits);
    /** @brief Current 64-bit collision mask. */
    uint64_t getMaskBits() const;
    /** @brief Collision group; negative never collides, positive always collides. */
    void setGroupIndex(int index);
    int getGroupIndex() const;

    /**
     * @brief Returns the owning body, or null after invalidation.
     * @return Borrowed nullable Body3D pointer owned by the physics world.
     * @ownership Shape3D does not own the body; callers must not delete it.
     * @lifetime Valid until body/world destruction; use a PhysicsBodyHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    Body3D *getBody() { return body_; }
    /**
     * @brief Returns the owning body for read-only link construction.
     * @return Borrowed nullable Body3D pointer owned by the physics world.
     * @ownership Shape3D does not own the body; callers must not delete it.
     * @lifetime Valid until body/world destruction; use a PhysicsBodyHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    const Body3D *getBody() const { return body_; }

    /** @brief Point-in-shape test in world meters. */
    bool testPoint(float x, float y, float z) const;

    /** @brief Destroys the shape inside its world. */
    void destroy();

    /** @brief Raw Box3D shape id / liveness. */
    b3ShapeId raw() const { return shapeId_; }
    bool      isValid() const;

    /** @brief Internal: marks the wrapper invalid after destruction. */
    void invalidate();

private:
    friend class World3D;
    friend class Body3D;

    void recreate(bool sensor);
    void enableContactOverrideFiltering();
    void refreshOneWayWorldData();
    bool allowsOneWayContact(double pointX, double pointY, double pointZ, float normalX,
                             float normalY, float normalZ) const;

    World3D  *world_ = nullptr;
    Body3D   *body_  = nullptr;
    b3ShapeId shapeId_{};
    PhysicsShapeHandle             runtimeHandle_ = PhysicsShapeHandle::invalid();
    Kind      kind_ = Kind::Box;
    float     a_ = 0.f;  // box hx | sphere r | capsule half-height
    float     b_ = 0.f;  // box hy | unused   | capsule radius
    float     c_ = 0.f;  // box hz
    std::vector<float> hullVertices_;
    int       hullMaxVertices_ = 64;
    std::vector<float> meshVertices_;
    std::vector<int32_t> meshIndices_;
    b3MeshData *meshData_ = nullptr;
    bool meshWeldVertices_ = true;
    float meshWeldTolerance_ = 0.001f;
    bool meshIdentifyEdges_ = true;
    bool meshUseMedianSplit_ = false;
    std::vector<uint8_t> meshMaterialIndices_;
    std::vector<b3SurfaceMaterial> meshMaterials_;
    bool meshMaterialsDirty_ = false;
    bool contactOverrideFiltering_ = false;
    float explosionScale_ = 1.f;
    std::vector<float> heightValues_;
    int heightCountX_ = 0, heightCountZ_ = 0;
    float heightCellSizeX_ = 1.f, heightCellSizeZ_ = 1.f;
    float heightGlobalMin_ = 0.f, heightGlobalMax_ = 0.f;
    bool heightClockwise_ = false;
    b3HeightFieldData *heightData_ = nullptr;
    int       id_ = 0;
    int       tag_ = 0;
    bool      hitEventsEnabled_ = false;
    float     localX_ = 0.f, localY_ = 0.f, localZ_ = 0.f;
    float     localQx_ = 0.f, localQy_ = 0.f, localQz_ = 0.f, localQw_ = 1.f;
    bool      oneWayEnabled_ = false;
    float     oneWayLocalNormalX_ = 0.f, oneWayLocalNormalY_ = 1.f,
              oneWayLocalNormalZ_ = 0.f;
    float     oneWayPlaneOffset_ = 0.f;
    float     oneWayMargin_ = 0.05f;
    float     oneWayMinNormalDot_ = 0.1f;
    double    oneWayWorldPointX_ = 0.0, oneWayWorldPointY_ = 0.0, oneWayWorldPointZ_ = 0.0;
    float     oneWayWorldNormalX_ = 0.f, oneWayWorldNormalY_ = 1.f,
              oneWayWorldNormalZ_ = 0.f;
};

}  // namespace eve::physics
