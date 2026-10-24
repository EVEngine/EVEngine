#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/SdfField3D.h"
#include "fluids/FluidSdf.h"
#include "physics/DistanceField3D.h"

namespace eve::fluids {

/**
 * @brief Borrows a MeshSdf as the shared ISdfField3D capability surface.
 *
 * @ownership Borrows the field; clear or destroy the adapter before destroying the field.
 * @thread Simulation-thread affine; no callbacks.
 * @details MeshSdf has no built-in revision counter; `bind` increments a local
 * revision so consumers can invalidate caches when the borrowed field is replaced.
 */
class EVENGINE_API_DOMAINS MeshSdfField final : public ISdfField3D {
public:
    /** @brief Constructs an unbound adapter. */
    MeshSdfField() = default;

    /**
     * @brief Bind a live MeshSdf and bump the adapter revision.
     * @param field Borrowed provider; must outlive this adapter while bound.
     */
    void bind(const MeshSdf& field);

    /** @brief Clears the borrowed binding. */
    void clear();

    /** @brief True when a field is currently bound. */
    [[nodiscard]] bool isBound() const { return field_ != nullptr; }

    [[nodiscard]] float                    sample(float x, float y, float z) const override;
    [[nodiscard]] SdfField3DGradientStatus gradient(float x, float y, float z, float& outNx, float& outNy,
                                                    float& outNz) const override;
    [[nodiscard]] unsigned                 revision() const override;
    void bounds(float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) const override;

private:
    const MeshSdf* field_    = nullptr;
    unsigned       revision_ = 0;
};

/**
 * @brief Copy a MeshSdf into the portable VertexGrid blob.
 * @details Boundary policy note: MeshSdf extends outside by Euclidean distance;
 * the blob's `outsideDistance` is a constant used by DistanceField3D import.
 * @cost Copies every distance sample once.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<SdfField3DBlob> sdfField3DBlobFromMeshSdf(const MeshSdf& sdf,
                                                                                    float outsideDistance = 1000.f);

/**
 * @brief Build a MeshSdf from a portable VertexGrid blob.
 * @cost Allocates and copies every sample once.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshSdf> meshSdfFromSdfField3DBlob(const SdfField3DBlob& blob);

/**
 * @brief Convert MeshSdf into a physics DistanceField3D (VertexGrid layout).
 * @details Outside the lattice DistanceField3D returns constant `outsideDistance`
 * instead of MeshSdf's Euclidean extension.
 * @cost Allocates the destination grid and copies every sample once.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<physics::DistanceField3D> distanceFieldFromMeshSdf(
    const MeshSdf& sdf, float outsideDistance = 1000.f);

/**
 * @brief Convert a physics DistanceField3D into a MeshSdf (VertexGrid layout).
 * @cost Allocates and copies every sample once.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshSdf> meshSdfFromDistanceField(const physics::DistanceField3D& field);

}  // namespace eve::fluids
