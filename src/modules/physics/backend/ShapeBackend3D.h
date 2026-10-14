#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "common/Result.h"
#include "physics/backend/RigidTypes3D.h"
namespace eve::physics {
class Shape3D;
/** @brief Geometry kinds supported by the rigid shape contract. */
enum class ShapeKind3D { Box, Sphere, Capsule, ConvexHull, TriangleMesh, HeightField };
/** @brief Owning, backend-independent shape source; metres, local quaternion XYZW. */
struct ShapeGeometry3D {
    ShapeKind3D          kind = ShapeKind3D::Box;
    float                a = 0.f, b = 0.f, c = 0.f;
    std::vector<float>   hullVertices;
    int                  hullMaxVertices = 64;
    std::vector<float>   meshVertices;
    std::vector<int32_t> meshIndices;
    bool                 meshWeldVertices   = true;
    float                meshWeldTolerance  = 0.001f;
    bool                 meshIdentifyEdges  = true;
    bool                 meshUseMedianSplit = false;
    std::vector<uint8_t> meshMaterialIndices;
    std::vector<float>   heightValues;
    int                  heightCountX = 0, heightCountZ = 0;
    float                heightCellSizeX = 1.f, heightCellSizeZ = 1.f;
    float                heightGlobalMin = 0.f, heightGlobalMax = 0.f;
    bool                 heightClockwise = false;
    float                localX = 0.f, localY = 0.f, localZ = 0.f;
    float                localQx = 0.f, localQy = 0.f, localQz = 0.f, localQw = 1.f;
};
/** @brief Owning surface values; userMaterialId contains the engine material ID and combine policy encoding. */
struct SurfaceMaterial3D {
    float           friction = 0.2f, restitution = 0.f, rollingResistance = 0.f;
    PhysicsVector3D tangentVelocity;
    uint64_t        userMaterialId = 0;
};
/** @brief Collision filter; negative groups never collide, positive groups always collide. */
struct ShapeFilter3D {
    uint64_t categoryBits = 1, maskBits = UINT64_MAX;
    int      groupIndex = 0;
};
/** @brief Shape behavior reapplied atomically when geometry changes. */
struct ShapeBehavior3D {
    bool  sensor = false, hitEvents = false, preSolveEvents = false, customFiltering = false;
    float explosionScale = 1.f;
};
/** @brief Owning world-space plane; double-precision point and unit normal. */
struct ShapePlane3D {
    double x = 0, y = 0, z = 0;
    float  nx = 0, ny = 1, nz = 0;
};

/**
 * @brief Sole owner of native shape and cooked resources; facade owns portable source geometry.
 * @thread Owning simulation thread, between steps; no user callbacks or concurrent mutation.
 * @lifetime Owning results survive mutation. Input views are borrowed only for this synchronous call.
 * @remarks Result failures leave native state unchanged. Native IDs and cooked data never escape.
 */
class IShapeBackend3D {
public:
    /** @brief Computes an owning world-space plane using the current body pose; no callbacks. */
    [[nodiscard]] virtual eve::Result<ShapePlane3D> oneWayPlane(const ShapeGeometry3D& geometry, PhysicsVector3D normal,
                                                                float offset) const = 0;
    virtual ~IShapeBackend3D()                                                      = default;
    /** @brief Native shape liveness. */
    virtual bool isValid() const noexcept = 0;
    /** @brief Idempotent native resource release after dependent records have been detached. */
    virtual void destroy() noexcept = 0;
    /** @brief Detaches native identity before parent teardown; retains cooked data until destruction. */
    virtual void invalidate() noexcept = 0;
    /** @brief Binds the owning facade for internal collision attribution; no callbacks.
     * @ownership Borrows its exclusive owner; owner calls invalidate before destruction.
     * @lifetime Reference remains valid until invalidation; cannot be rebound while stepping.
     */
    virtual void bindOwner(Shape3D& owner) noexcept = 0;
    /** @brief Atomically replaces cooked geometry, preserving material, density and filter.
     * @cost One geometry cook and shape replacement per edit; input views are consumed synchronously.
     * @return InvalidArgument/Unsupported/Failed without replacing the previous native shape.
     */
    [[nodiscard]] virtual eve::Result<void> rebuild(const ShapeGeometry3D& geometry, ShapeBehavior3D behavior,
                                                    std::span<const SurfaceMaterial3D> materials) = 0;
    /** @brief density; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<float> density() const = 0;
    /** @brief setDensity; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> setDensity(float value, bool updateMass) = 0;
    /** @brief friction; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<float> friction() const = 0;
    /** @brief setFriction; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> setFriction(float value) = 0;
    /** @brief restitution; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<float> restitution() const = 0;
    /** @brief setRestitution; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> setRestitution(float value) = 0;
    /** @brief material; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<SurfaceMaterial3D> material() const = 0;
    /** @brief setMaterial; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> setMaterial(SurfaceMaterial3D value) = 0;
    /** @brief filter; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<ShapeFilter3D> filter() const = 0;
    /** @brief setFilter; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> setFilter(ShapeFilter3D value, bool updatePairs) = 0;
    /** @brief isSensor; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<bool> isSensor() const = 0;
    /** @brief enableHitEvents; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> enableHitEvents(bool enabled) = 0;
    /** @brief enablePreSolveEvents; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> enablePreSolveEvents(bool enabled) = 0;
    /** @brief meshMaterialCount; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<int> meshMaterialCount() const = 0;
    /** @brief meshMaterial; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<SurfaceMaterial3D> meshMaterial(int slot) const = 0;
    /** @brief setMeshMaterial; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<void> setMeshMaterial(SurfaceMaterial3D value, int slot) = 0;
    /** @brief testPoint; synchronous owning-value operation under the class lifetime contract. */
    [[nodiscard]] virtual eve::Result<bool> testPoint(float x, float y, float z) const = 0;
};
}  // namespace eve::physics
