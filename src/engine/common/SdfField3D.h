#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <vector>

namespace eve {

/** @brief Outcome of sampling a 3D SDF gradient. */
enum class SdfField3DGradientStatus {
    Defined,    ///< Finite outward unit gradient written to the output components.
    Undefined,  ///< Empty field or singularity; output components are unspecified.
};

/**
 * @brief Dense regular-grid sample layout used by the shared SDF interchange blob.
 *
 * VertexGrid matches `physics::DistanceField3D` and the sample lattice of
 * `fluids::MeshSdf` (origin at the first sample, x-fastest then y then z).
 */
enum class SdfField3DSampleKind : std::uint8_t {
    VertexGrid = 1,
};

/**
 * @brief Borrowed 3D signed-distance field for cross-module collision queries.
 *
 * Negative samples are inside the solid. Providers live in higher modules
 * (physics, fluids, gameplay) and register through capability wiring; consumers
 * only sample this interface and never include provider headers.
 *
 * @ownership Borrowed for the duration of the consumer binding. Callers must
 *            clear the binding before destroying the field.
 * @thread Owner thread only; not reentrant from unknown callbacks.
 */
class EVENGINE_API_FOUNDATION_INLINE ISdfField3D {
public:
    static constexpr const char* capabilityName = "ISdfField3D";

    /** @brief I sdf field 3 d. */
    virtual ~ISdfField3D() = default;

    /**
     * @brief Sample signed distance at a world-space position.
     * @return Finite signed distance in world units, or +inf when unsupported.
     */
    [[nodiscard]] virtual float sample(float x, float y, float z) const = 0;

    /**
     * @brief Approximate outward unit gradient at a world-space position.
     * @param[out] outNx X component of the unit gradient when the status is Defined.
     * @param[out] outNy Y component of the unit gradient when the status is Defined.
     * @param[out] outNz Z component of the unit gradient when the status is Defined.
     * @return Defined when a finite gradient was written; Undefined at empty
     *         fields or singularities.
     */
    [[nodiscard]] virtual SdfField3DGradientStatus gradient(float x, float y, float z, float& outNx, float& outNy,
                                                            float& outNz) const = 0;

    /**
     * @brief Monotonic revision incremented after each successful field mutation.
     * @details Consumers use this to detect stale GPU uploads or cached contacts.
     */
    [[nodiscard]] virtual unsigned revision() const = 0;

    /**
     * @brief Axis-aligned world bounds of the sampled lattice (inclusive corners).
     * @param[out] minX Minimum X of the sample lattice.
     * @param[out] minY Minimum Y of the sample lattice.
     * @param[out] minZ Minimum Z of the sample lattice.
     * @param[out] maxX Maximum X of the sample lattice.
     * @param[out] maxY Maximum Y of the sample lattice.
     * @param[out] maxZ Maximum Z of the sample lattice.
     */
    virtual void bounds(float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) const = 0;
};

/**
 * @brief Portable dense SDF asset blob (schema id + version + samples).
 *
 * Interchange between physics DistanceField3D and fluids MeshSdf. Outside the
 * lattice, DistanceField3D uses constant `outsideDistance`; MeshSdf extends by
 * Euclidean distance — consumers must document which boundary policy they apply
 * after import.
 */
struct SdfField3DBlob {
    static constexpr const char* schemaId      = "eve.sdf.field3d";
    static constexpr int         schemaVersion = 1;

    SdfField3DSampleKind kind            = SdfField3DSampleKind::VertexGrid;
    int                  width           = 0;
    int                  height          = 0;
    int                  depth           = 0;
    float                cellSize        = 1.f;
    float                originX         = 0.f;
    float                originY         = 0.f;
    float                originZ         = 0.f;
    float                outsideDistance = 1000.f;
    /** @brief Sample buffer in x-fastest, then y, then z order. */
    std::vector<float> distances;
};

/**
 * @brief Encode a validated SDF blob to a Value object.
 * @cost Copies the distance sample buffer into the Value array once.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> encodeSdfField3DBlob(const SdfField3DBlob& blob);

/**
 * @brief Decode a Value object into an owning SDF blob.
 * @cost Allocates and copies the distance sample buffer once on success.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<SdfField3DBlob> decodeSdfField3DBlob(const Value& value);

/**
 * @brief Validate dimensions, finiteness and sample count without encoding.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> validateSdfField3DBlob(const SdfField3DBlob& blob);

}  // namespace eve
