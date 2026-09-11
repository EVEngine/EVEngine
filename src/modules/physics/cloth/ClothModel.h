#pragma once

#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::physics {

/** @brief Type of a precomputed distance constraint in a cloth model. */
enum class ClothConstraintKind : uint8_t {
    Structural,
    Shear,
    Bend,
};

/** @brief Immutable rest-space particle stored by a ClothModel. */
struct ClothModelParticle {
    float x           = 0.f;
    float y           = 0.f;
    float z           = 0.f;
    float inverseMass = 1.f;
};

/** @brief Triangle indices into a ClothModel particle array. */
struct ClothModelTriangle {
    int vertices[3] = {0, 0, 0};
};

/** @brief Precomputed rest-length constraint between two cloth particles. */
struct ClothModelDistanceConstraint {
    int                 a          = 0;
    int                 b          = 0;
    float               restLength = 0.f;
    ClothConstraintKind kind       = ClothConstraintKind::Structural;
};

/** @brief Adjacent-triangle fold relationship around the shared edge (a,b). */
struct ClothModelFoldConstraint {
    int a         = 0;
    int b         = 0;
    int oppositeA = 0;
    int oppositeB = 0;
};

/** @brief Geodesic maximum-distance constraint from a particle to a fixed anchor. */
struct ClothModelTetherConstraint {
    int   particle  = 0;
    int   anchor    = 0;
    float maxLength = 0.f;
};

/**
 * @brief Reusable, validated cloth simulation asset.
 *
 * ClothModel owns rest-space particles, triangle topology and precomputed
 * constraints. It contains no per-frame solver state, renderer object or
 * collision-world reference, so one model may be copied into multiple cloth
 * runtime instances. Construction is transactional: invalid topology produces
 * a failed Result and no partially usable model.
 *
 * @thread Construct off-thread when desired, then treat the published model as
 * read-only. Accessors are safe for concurrent readers when no thread mutates
 * the same ClothModel object.
 * @reentrancy Construction and access invoke no callbacks.
 */
class ClothModel {
public:
    static constexpr uint32_t SchemaVersion = 1;

    /**
     * @brief Bake an arbitrary indexed triangle mesh into a cloth model.
     * @param positions Packed XYZ rest positions; size must be a non-zero multiple of three.
     * @param triangleIndices Triangle indices; size must be a non-zero multiple of three.
     * @param inverseMasses Optional per-particle inverse masses. Zero pins a particle.
     * @return Validated owning model, or a diagnostic without partial output.
     */
    [[nodiscard("check cloth model bake outcome")]]
    static eve::Result<ClothModel> fromTriangles(std::span<const float> positions, std::span<const int> triangleIndices,
                                                 std::span<const float> inverseMasses = {});

    /**
     * @brief Bake a regular XZ cloth grid with its top row pinned.
     * @param cols Grid columns, at least two.
     * @param rows Grid rows, at least two.
     * @param spacing Positive particle spacing in meters.
     * @param originX Rest-space origin X.
     * @param originY Rest-space origin Y.
     * @param originZ Rest-space origin Z.
     * @return Validated owning model, or a diagnostic without partial output.
     */
    [[nodiscard("check cloth grid model outcome")]]
    static eve::Result<ClothModel> grid(int cols, int rows, float spacing, float originX, float originY, float originZ);

    /** @brief Decode strict `eve.cloth-model/1` data; unknown fields are rejected. */
    [[nodiscard("check cloth model decode outcome")]]
    static eve::Result<ClothModel> fromValue(const eve::Value& value);

    /** @brief Parse and decode strict UTF-8 `eve.cloth-model/1` JSON. */
    [[nodiscard("check cloth model JSON decode outcome")]]
    static eve::Result<ClothModel> fromJson(std::string_view json);

    /** @brief Encode canonical owning `eve.cloth-model/1` data. */
    [[nodiscard]] eve::Value toValue() const;

    /** @brief Serialize deterministic compact `eve.cloth-model/1` JSON. */
    [[nodiscard("check cloth model JSON encode outcome")]]
    eve::Result<std::string> toJson() const;

    /** @brief Schema version used by serialization adapters. */
    [[nodiscard]] uint32_t schemaVersion() const noexcept { return SchemaVersion; }
    /** @brief Read-only particles, valid until this model is destroyed or assigned. */
    [[nodiscard]] std::span<const ClothModelParticle> particles() const noexcept { return particles_; }
    /** @brief Read-only triangles, valid until this model is destroyed or assigned. */
    [[nodiscard]] std::span<const ClothModelTriangle> triangles() const noexcept { return triangles_; }
    /** @brief Read-only distance constraints, valid until this model is destroyed or assigned. */
    [[nodiscard]] std::span<const ClothModelDistanceConstraint> distanceConstraints() const noexcept {
        return distanceConstraints_;
    }
    /** @brief Read-only fold constraints, valid until this model is destroyed or assigned. */
    [[nodiscard]] std::span<const ClothModelFoldConstraint> foldConstraints() const noexcept {
        return foldConstraints_;
    }
    /** @brief Read-only geodesic tethers generated from fixed particles. */
    [[nodiscard]] std::span<const ClothModelTetherConstraint> tetherConstraints() const noexcept {
        return tetherConstraints_;
    }
    /** @brief Whether every topology edge belongs to exactly two triangles. */
    [[nodiscard]] bool isClosed() const noexcept { return closed_; }
    /** @brief Signed rest volume for a consistently wound closed mesh, otherwise zero. */
    [[nodiscard]] float restVolume() const noexcept { return restVolume_; }
    /** @brief Grid column metadata, or zero for an arbitrary mesh. */
    [[nodiscard]] int gridCols() const noexcept { return gridCols_; }
    /** @brief Grid row metadata, or zero for an arbitrary mesh. */
    [[nodiscard]] int gridRows() const noexcept { return gridRows_; }
    /** @brief Grid spacing metadata, or zero for an arbitrary mesh. */
    [[nodiscard]] float gridSpacing() const noexcept { return gridSpacing_; }

private:
    std::vector<ClothModelParticle>           particles_;
    std::vector<ClothModelTriangle>           triangles_;
    std::vector<ClothModelDistanceConstraint> distanceConstraints_;
    std::vector<ClothModelFoldConstraint>     foldConstraints_;
    std::vector<ClothModelTetherConstraint>   tetherConstraints_;
    bool                                      closed_      = false;
    float                                     restVolume_  = 0.f;
    int                                       gridCols_    = 0;
    int                                       gridRows_    = 0;
    float                                     gridSpacing_ = 0.f;
};

}  // namespace eve::physics
