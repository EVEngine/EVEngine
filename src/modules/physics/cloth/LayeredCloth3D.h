#pragma once
#include <memory>
#include <span>
#include "physics/cloth/Cloth3D.h"

namespace eve::physics {
/**
 * @brief Owns multiple cloth sheets and advances their contacts synchronously.
 * @ownership Owns every layer; no external runtime pointers are retained.
 * @thread All operations run on the owning physics thread, between draw calls.
 * @reentrancy No callbacks. No concurrent calls or layer updates during update().
 * Runtime-only state; no persistence or GPU parity is claimed. Replays use fixed
 * dt and stable layer order, with a 1e-5 metre tolerance on the same CPU build.
 */
class EVENGINE_API_DOMAINS LayeredCloth3D {
public:
    /** @brief Construct parallel XZ grids, separated along +Y.
     * @throws eve::Exception for invalid grid, count, gap or coordinates.
     * @cost Allocation and constraint baking scale with total grid particles;
     * construct once and amortize over simulation frames.
     */
    LayeredCloth3D(int count, int cols, int rows, float spacing, float gap, float x, float y, float z);
    /** @brief Copy validated arbitrary mesh models; source span is borrowed only during construction.
     * @throws eve::Exception if the model span is empty.
     * @cost Copies topology and allocates each runtime once, amortized over frames.
     */
    explicit LayeredCloth3D(std::span<const ClothModel> models);
    /** @brief Release owned runtime layers; render resources follow the graphics owner. */
    ~LayeredCloth3D();
    /** @brief Borrow a layer for synchronous configuration; valid until owner destruction.
     * Do not destroy or independently update it while the group is active.
     * @throws eve::Exception for invalid index.
     */
    Cloth3D& layer(int index);
    /** @brief Return the number of owned sheets. */
    [[nodiscard]] int getLayerCount() const;
    /** @brief Advance all layers by finite dt in [0,0.05], without dropping time.
     * @throws eve::Exception on invalid dt or a destroyed layer, before mutation.
     * @cost Substeps times constraint iterations plus broad-phase candidate
     * triangle contacts; dense overlaps can be quadratic in triangle count.
     */
    void update(float dt);
    /** @brief Configure substeps [1,32] and coupling passes [1,16]; throws on invalid input. */
    void setSolver(int substeps, int passes);
    /** @brief Enable or disable only contacts between different sheets. */
    void setInterCollision(bool enabled);
    /** @brief Return whether inter-layer contacts are enabled. */
    [[nodiscard]] bool getInterCollision() const;
    /** @brief Return projected contacts in the most recent update, including repeated passes. */
    [[nodiscard]] int getContactCount() const;
    /** @brief Set the air velocity of every sheet; throws on nonfinite input. */
    void setWindVelocity(float x, float y, float z);
    /** @brief Restore every layer's authored pose and clear contact statistics. */
    void reset();
    /** @brief Draw owned cloth surfaces in an open 3D frame.
     * @param gfx Borrowed graphics ABI pointer, used only for this call; must be nonnull.
     */
    void draw(graphics::Graphics* gfx);

private:
    void                                  solveContacts(Cloth3D& a, Cloth3D& b);
    std::vector<std::unique_ptr<Cloth3D>> layers_;
    int                                   substeps_       = 4;
    int                                   passes_         = 3;
    int                                   contacts_       = 0;
    bool                                  interCollision_ = true;
};
}  // namespace eve::physics
