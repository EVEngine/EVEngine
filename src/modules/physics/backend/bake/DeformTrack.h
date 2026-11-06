#pragma once
#include <memory>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/backend/bake/DeformTopology.h"
namespace eve::physics {
class BakeRecipe;
/** @brief Owning position in the explicit coordinates recorded by the recipe. */
struct DeformPoint {
    float x = 0, y = 0, z = 0;
};
/** @brief Owning snapshot with actual connectivity, stable indexed positions and explicit sample time. */
struct DeformFrame {
    float                    seconds;
    DeformTopology           topology;
    std::vector<DeformPoint> positions;
};
/** @brief Immutable fixed-topology deformation clip, independent of simulation and rendering providers.
 * @ownership Shares frozen frames and a copied recipe; input mutation or source destruction cannot affect playback.
 * @thread Const access is reentrant with independent output buffers. No callbacks, wall clock reads or solver calls.
 * @remarks Vertex indices remain stable throughout the clip. Connectivity or UV changes require a new clip.
 */
class EVENGINE_API_BACKENDS DeformTrack {
public:
    /** @brief Whether both views share the same frozen asset; constant-time identity query. */
    [[nodiscard]] bool isSameAsset(const DeformTrack& other) const noexcept { return data_ == other.data_; }
    /** @brief Validate matching topology, positions, coordinates and fixed sample grid, then freeze all frames.
     * @return Complete clip or failure. At most 2000000 total vertex samples; no partially published state.
     * @cost Linear in sampled vertices and topology validation; prepare once outside playback.
     */
    [[nodiscard]] static Result<DeformTrack> prepare(std::vector<DeformFrame> frames, const BakeRecipe& recipe);
    /** @brief Publish EVEDEFM1 v1 with topology, provenance, frames and whole-file checksum; never overwrite.
     * @cost Linear in asset size plus filesystem barriers; once per baked asset outside playback.
     * @return Checked publication status using publishBakedAsset durability semantics.
     */
    [[nodiscard]] Result<void> save(std::string_view path) const;
    /** @brief Load bounded EVEDEFM1 v1 and validate all data before publishing a clip.
     * @cost Linear in asset size plus validation, once per load outside playback.
     * @return Complete owning clip or failure. Unknown versions and trailing data are rejected; no older schema exists.
     */
    [[nodiscard]] static Result<DeformTrack> load(std::string_view path);
    /** @brief Load and compare complete canonical expected provenance before cache admission.
     * @cost Linear in asset size plus validation, once per admission outside playback.
     * @return Clip or structured mismatch, malformed-data or filesystem diagnostic; never rebakes implicitly.
     */
    [[nodiscard]] static Result<DeformTrack> loadForRecipe(std::string_view path, const BakeRecipe& expected);
    /** @brief Interpolate all vertices into an exact-sized caller buffer; no allocation or solver work.
     * @ownership Borrows output only during this call; never retains its address.
     * @return Success or invalid time/output extent; failure leaves output untouched. Time clamps to endpoints.
     * @cost Linear in vertices plus logarithmic frame search, per sampled presentation frame.
     */
    [[nodiscard]] Result<void> sample(float seconds, std::span<DeformPoint> output) const;
    /** @brief Return a cheap owning copy of the fixed connectivity and UVs. */
    [[nodiscard]] DeformTopology topology() const;
    /** @brief Return shared immutable provenance including explicit coordinates. */
    [[nodiscard]] std::shared_ptr<const BakeRecipe> recipe() const noexcept;
    /** @brief Last sample time in seconds. */
    [[nodiscard]] float duration() const noexcept;
    /** @brief Number of immutable frames. */
    [[nodiscard]] std::size_t frameCount() const noexcept;

private:
    struct Data;
    explicit DeformTrack(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::physics
