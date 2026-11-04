#pragma once
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"

namespace eve::physics {
class BakeRecipe;
/** @brief Explicit sample coordinates; planar pixels preserve the producer's XY axes without implicit Y flip. */
enum class RigidTrackSpace { PlanarPixels, Metres3D };
/** @brief Owning pose; XYZW quaternion, pixels or metres selected by the track. Planar poses require z=qx=qy=0. */
struct RigidTrackPose {
    float x = 0, y = 0, z = 0;
    float qx = 0, qy = 0, qz = 0, qw = 1;
};
/** @brief Stable caller-supplied object identity paired with a pose; never a runtime body handle. */
struct RigidTrackSample {
    PersistentId   id;
    RigidTrackPose pose;
};
/** @brief Owning frame at an explicit finite simulation time, starting at zero. */
struct RigidTrackFrame {
    float                         seconds = 0;
    std::vector<RigidTrackSample> samples;
};
/** @brief Frozen fixed-roster rigid poses, independent of physics, graphics and script providers.
 * @ownership Copies share immutable owning data; prepare consumes values and retains no caller aliases.
 * @thread Const operations and separate copies are reentrant. No callbacks, clock reads or IO during sampling.
 * @lifetime Samples and identities survive destruction of the original recording and simulation world.
 * Frames may arrive with different sample order, but missing, duplicate or new IDs are rejected.
 * Position interpolation is linear; rotations use shortest-path normalized interpolation, not angular turns.
 */
class EVENGINE_API_BACKENDS RigidPoseTrack {
public:
    /** @brief Validate, normalize and freeze a bounded recording with stable sorted track identities.
     * @cost Linear in samples plus per-frame identity sorting; prepare once outside the frame loop.
     * @return Complete track or failure without modifying any existing track.
     */
    [[nodiscard]] static Result<RigidPoseTrack> prepare(RigidTrackSpace space, std::vector<RigidTrackFrame> frames);
    /** @brief Validate and freeze recording plus complete matching coordinate/timing provenance.
     * @cost Linear in samples plus roster sorting; prepare once before publication, outside playback.
     * @return Owning immutable track with a frozen recipe, or failure without publishing partial state.
     */
    [[nodiscard]] static Result<RigidPoseTrack> prepare(RigidTrackSpace space, std::vector<RigidTrackFrame> frames,
                                                        const BakeRecipe& recipe);
    /** @brief Return shared immutable provenance; empty for explicit assets without verified origins. */
    [[nodiscard]] std::shared_ptr<const BakeRecipe> recipe() const noexcept;
    /** @brief Load an asset and require exact canonical expected provenance before cache admission.
     * @cost Linear in asset bytes plus validation; once per cache admission, outside playback.
     * @return Track or diagnostic for missing provenance, mismatch, corruption or IO failure; never rebakes.
     */
    [[nodiscard]] static Result<RigidPoseTrack> loadForRecipe(std::string_view path, const BakeRecipe& expected);
    /** @brief Publish an immutable track without replacing an existing asset.
     * @cost Linear in samples plus filesystem barriers; save once outside playback.
     * @return Checked publication status, with the same failure semantics as publishBakedAsset.
     * EVERIGD1 v1 stores explicit assets without provenance; v2 embeds complete validated provenance.
     * Both store coordinates, sorted persistent IDs, frame data and checksum. Cache admission requires loadForRecipe.
     */
    [[nodiscard]] Result<void> save(std::string_view path) const;
    /** @brief Read and validate a bounded EVERIGD1 v1/v2 asset before returning an immutable track.
     * @cost Linear in file size plus validation; load once and share across players.
     * @return Complete track or failure. Unknown schemas/versions, trailing fields and corrupt data are rejected.
     * Legacy v1 remains explicit-playback-only; attaching verified provenance requires preparing the recording again.
     */
    [[nodiscard]] static Result<RigidPoseTrack> load(std::string_view path);
    /** @brief Sample a known stable identity at finite seconds, clamping outside the recorded interval.
     * @return Owning pose or checked invalid-time/unknown-identity failure.
     */
    [[nodiscard]] Result<RigidTrackPose> sample(float seconds, PersistentId id) const;
    /** @brief Coordinate contract of every pose in this track. */
    [[nodiscard]] RigidTrackSpace space() const noexcept;
    /** @brief Last recorded timestamp. */
    [[nodiscard]] float duration() const noexcept;
    /** @brief Number of identities in the frozen roster. */
    [[nodiscard]] std::size_t trackCount() const noexcept;
    /** @brief Number of captured frames. */
    [[nodiscard]] std::size_t frameCount() const noexcept;
    /** @brief Stable identity at canonical sorted index; returns failure for an out-of-range index. */
    [[nodiscard]] Result<PersistentId> trackId(std::size_t index) const;

private:
    struct Data;
    static Result<RigidPoseTrack> prepareValidated(RigidTrackSpace space, std::vector<RigidTrackFrame> frames,
                                                   std::shared_ptr<const BakeRecipe> recipe);
    explicit RigidPoseTrack(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::physics
