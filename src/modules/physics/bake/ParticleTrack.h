#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
namespace eve::physics {
class BakeRecipe;
/** @brief Owning planar sprite presentation; pixels, unwrapped degrees, linear RGBA and normalized UV. */
struct BakedParticlePose {
    float x = 0, y = 0, w = 0, h = 0, rotation = 0;
    float r = 1, g = 1, b = 1, a = 1;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};
/** @brief Explicit event/sample time and owning presentation value. */
struct ParticleTrackKey {
    float             seconds = 0;
    BakedParticlePose pose;
};
/** @brief One birth's complete trajectory, including keys exactly at birth and death.
 * @remarks Emitter identifies a Sim ownership session; birthSerial is nonzero and never reused in that session.
 * First key is inclusive birth, last key exclusive death. Events may lie between the recipe's sampling ticks.
 */
struct ParticleTrajectory {
    PersistentId                  emitter;
    std::uint64_t                 birthSerial = 0;
    std::vector<ParticleTrackKey> keys;
};
/** @brief Owning visible sample retaining the two-part stable identity. */
struct BakedParticleSample {
    PersistentId      emitter;
    std::uint64_t     birthSerial = 0;
    BakedParticlePose pose;
};
/** @brief Immutable particle trajectories independent of live simulation, ECS and graphics resources.
 * @ownership Shares frozen values and provenance. No input aliases or resource pointers are retained.
 * @thread Const sampling is reentrant with independent output buffers; no callbacks or implicit time source.
 */
class EVENGINE_API_BACKENDS ParticleTrack {
public:
    /** @brief Validate and freeze trajectories in deterministic identity order and an explicit clip duration.
     * @cost Linear in keys plus trajectory sorting, once before playback. Limited to 2 million keys and 65536 births.
     * @return Complete planar-pixel track or diagnostic; failure publishes no partial state.
     */
    [[nodiscard]] static Result<ParticleTrack> prepare(float duration, std::vector<ParticleTrajectory> trajectories,
                                                       const BakeRecipe& recipe);
    /** @brief Publish EVEPART1 v1 with provenance and whole-file checksum, without overwriting any target.
     * @cost Linear in asset bytes plus durable file publication, once outside playback.
     * @return Checked publication result; uses publishBakedAsset platform durability semantics.
     */
    [[nodiscard]] Result<void> save(std::string_view path) const;
    /** @brief Load a bounded EVEPART1 v1 asset and validate all trajectories before publication.
     * @cost Linear in file bytes plus validation/sorting, once per load outside playback.
     * @return Complete owning track or diagnostic. Unknown versions/fields and trailing bytes are rejected.
     * No earlier schema exists; future migration must be explicit.
     */
    [[nodiscard]] static Result<ParticleTrack> load(std::string_view path);
    /** @brief Load and require an exact canonical provenance match for cache admission.
     * @cost Linear in asset bytes plus validation, once per admission outside playback.
     * @return Track or checked failure; never implicitly rebakes or replaces another asset.
     */
    [[nodiscard]] static Result<ParticleTrack> loadForRecipe(std::string_view path, const BakeRecipe& expected);
    /** @brief Sample visible births into a caller buffer of at least capacity() entries; no allocation.
     * @ownership Borrows output only during this call. Failure leaves all entries unchanged; unused tail is untouched.
     * @return Number of written entries or diagnostic. Time clamps to the clip; deaths are exclusive even at its end.
     * @remarks Position, size, unwrapped rotation and RGBA interpolate linearly. Atlas UV holds the preceding key.
     * @cost Linear in births plus logarithmic search per visible trajectory, per presentation frame.
     */
    [[nodiscard]] Result<std::size_t> sample(float seconds, std::span<BakedParticleSample> output) const;
    /** @brief Maximum output extent, equal to the total birth count. */
    [[nodiscard]] std::size_t capacity() const noexcept;
    /** @brief Explicit clip duration including silent intervals. */
    [[nodiscard]] float duration() const noexcept;
    /** @brief Owning shared immutable provenance. */
    [[nodiscard]] std::shared_ptr<const BakeRecipe> recipe() const noexcept;

private:
    struct Data;
    explicit ParticleTrack(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::physics
