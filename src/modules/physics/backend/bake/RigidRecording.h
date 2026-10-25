#pragma once
#include <cstddef>
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
class BakeRecipe;
enum class RigidTrackSpace;
class RigidPoseTrack;
struct RigidTrackFrame;
/** @brief Bounded owning recorder of stable-identity rigid frames, without simulation dependencies.
 * @ownership Owns recipe and accepted values; never retains body/world/VM pointers or runtime handles.
 * @thread Exclusive owner-thread mutation; no callbacks, implicit clock reads or stepping.
 * @remarks Move-only. Moved-from instances may only be destroyed or assigned.
 */
class EVENGINE_API_BACKENDS RigidRecording {
public:
    /** @brief Prepare from complete provenance and total pose-sample budget in [1,2000000].
     * @remarks Coordinate space is taken from the recipe; first frame establishes a roster of up to 16384 IDs.
     * @cost Linear in bounded recipe metadata, once before recording; frame storage grows on append.
     * @return Empty recorder or checked invalid metadata/budget/allocation failure.
     */
    [[nodiscard]] static Result<RigidRecording> prepare(const BakeRecipe& recipe, std::size_t sampleBudget);
    ~RigidRecording();
    RigidRecording(RigidRecording&&) noexcept;
    RigidRecording& operator=(RigidRecording&&) noexcept;
    RigidRecording(const RigidRecording&)            = delete;
    RigidRecording& operator=(const RigidRecording&) = delete;
    /** @brief Validate, identity-sort and normalize an owning candidate at the next recipe sample time.
     * @ownership Consumes candidate values; retains no caller alias. Rejection preserves the accepted recording.
     * @cost Linear pose validation plus identity sorting per frame; storage growth is amortized.
     * @return Checked identity/time/coordinate/quaternion/budget/allocation status, with no partial append.
     */
    [[nodiscard]] Result<void> append(RigidTrackFrame frame);
    /** @brief Freeze an independent immutable track; continued recording cannot mutate it.
     * @cost Linear copy/validation plus per-frame identity sorting, once per asset outside playback.
     * @return Complete track or failure without changing session state; empty sessions cannot freeze.
     */
    [[nodiscard]] Result<RigidPoseTrack> freeze() const;
    /** @brief Coordinate space fixed by the admitted recipe. */
    [[nodiscard]] RigidTrackSpace space() const noexcept;
    /** @brief Number of accepted frames. */
    [[nodiscard]] std::size_t frameCount() const noexcept;
    /** @brief Number of accepted poses across all frames. */
    [[nodiscard]] std::size_t sampleCount() const noexcept;

private:
    struct Impl;
    explicit RigidRecording(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::physics
