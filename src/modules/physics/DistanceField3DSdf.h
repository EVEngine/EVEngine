#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/SdfField3D.h"
#include "physics/DistanceField3D.h"

namespace eve::physics {

/**
 * @brief Borrows a DistanceField3D as the shared ISdfField3D capability surface.
 *
 * @ownership Borrows the field; clear or destroy the adapter before destroying the field.
 * @thread Simulation-thread affine; no callbacks.
 */
class EVENGINE_API_WORLD DistanceField3DSdf final : public ISdfField3D {
public:
    /** @brief Constructs an unbound adapter. */
    DistanceField3DSdf() = default;

    /**
     * @brief Bind a live distance field; revision tracks the field's own counter.
     * @param field Borrowed provider; must outlive this adapter while bound.
     */
    void bind(const DistanceField3D& field);

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
    const DistanceField3D* field_ = nullptr;
};

/**
 * @brief Copy a DistanceField3D into the portable VertexGrid blob.
 * @cost Copies every distance sample once.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<SdfField3DBlob> sdfField3DBlobFromDistanceField(const DistanceField3D& field);

/**
 * @brief Build a DistanceField3D from a portable VertexGrid blob.
 * @cost Allocates the destination grid and copies every sample once.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<DistanceField3D> distanceFieldFromSdfField3DBlob(const SdfField3DBlob& blob);

}  // namespace eve::physics
