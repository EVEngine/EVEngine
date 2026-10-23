#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <span>
#include <vector>

namespace eve::procgen {
class MeshBuild;

/**
 * @brief Owning regular scalar volume; x is contiguous, followed by y and z.
 * @remarks Samples use the caller's coordinate system. Cave volumes span [-1,1] on each axis;
 * nonnegative density is rock. Copies are independent. No ECS, VM or graphics resources are retained.
 * @thread One owner thread for mutation; independent copies may be processed on other threads.
 */
class EVENGINE_API_DOMAINS ScalarField3D {
public:
    /** @brief Atomically allocate a finite volume with axis sizes in [2,256].
     * @cost Linear in sample count; reuse a field for multiple edits before replacing a generation stage.
     */
    [[nodiscard]] Result<void> resize(int nx, int ny, int nz, float value);
    /** @brief Atomically copy finite samples of a correctly sized volume.
     * @cost Linear in sample count, amortized across subsequent field reads and edits.
     */
    [[nodiscard]] Result<void> assign(int nx, int ny, int nz, std::span<const float> samples);
    /** @brief Return one sample or an out-of-bounds diagnostic. */
    [[nodiscard]] Result<float> sample(int x, int y, int z) const;
    /** @brief Replace one finite sample; invalid input leaves the volume unchanged. */
    [[nodiscard]] Result<void> setSample(int x, int y, int z, float value);
    /** @brief Carve an ellipsoid from rock in normalized [-1,1] coordinates; bounded positive radii required.
     * @cost Linear in volume samples; compose multiple shapes before extracting a mesh.
     */
    [[nodiscard]] Result<void> carveEllipsoid(float x, float y, float z, float rx, float ry, float rz);
    /** @brief Extract an independent isosurface in [-0.5,0.5]; empty output returns a diagnostic.
     * @cost Linear in volume cells plus emitted triangles; reuse the result until the field changes.
     */
    [[nodiscard]] Result<MeshBuild> extractMesh(float isolevel) const;
    /** @brief Number of samples along X. */
    [[nodiscard]] int width() const noexcept { return nx_; }
    /** @brief Number of samples along Y. */
    [[nodiscard]] int height() const noexcept { return ny_; }
    /** @brief Number of samples along Z. */
    [[nodiscard]] int depth() const noexcept { return nz_; }
    /** @brief Borrow samples until this field is mutated, moved or destroyed; never retain across tasks. */
    [[nodiscard]] std::span<const float> samples() const noexcept { return samples_; }

private:
    [[nodiscard]] bool contains(int x, int y, int z) const noexcept;
    int                nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<float> samples_;
};

}  // namespace eve::procgen
