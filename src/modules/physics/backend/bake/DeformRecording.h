#pragma once
#include <cstddef>
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
class BakeRecipe;
class DeformTrack;
struct DeformFrame;
/** @brief Bounded owning builder for fixed-topology deformation recordings.
 * @ownership Owns a frozen recipe copy and accepted value frames, never a source/VM/world pointer.
 * @thread Exclusive owner-thread mutation; no callbacks, IO, clocks or solver calls.
 * @remarks Move-only. Moved-from instances may only be destroyed or assigned.
 */
class EVENGINE_API_BACKENDS DeformRecording {
public:
    /** @brief Admit a complete recipe and explicit total vertex-sample budget in [1,2000000].
     * @cost Linear in bounded recipe metadata, once before recording; frame storage grows during append.
     * @return Ready empty session or checked invalid recipe/budget/allocation failure.
     */
    [[nodiscard]] static Result<DeformRecording> prepare(const BakeRecipe& recipe, std::size_t sampleBudget);
    ~DeformRecording();
    DeformRecording(DeformRecording&&) noexcept;
    DeformRecording& operator=(DeformRecording&&) noexcept;
    DeformRecording(const DeformRecording&)            = delete;
    DeformRecording& operator=(const DeformRecording&) = delete;
    /** @brief Validate and append one owning candidate at the next recipe sample time.
     * @ownership Consumes candidate values, retaining no caller aliases; rejection preserves all session state.
     * @cost Linear in candidate vertices/connectivity; frame-vector growth is amortized over the recording.
     * @return Checked finite/coordinate/topology/grid/budget admission or allocation failure.
     */
    [[nodiscard]] Result<void> append(DeformFrame frame);
    /** @brief Freeze an independent immutable snapshot; the session remains available for further appends.
     * @cost Linear copy and validation of all accepted samples, once per published clip outside playback.
     * @return Complete track or failure; neither existing tracks nor session state change on failure.
     */
    [[nodiscard]] Result<DeformTrack> freeze() const;
    /** @brief Number of successfully accepted frames. */
    [[nodiscard]] std::size_t frameCount() const noexcept;
    /** @brief Total accepted vertex samples, excluding rejected candidates. */
    [[nodiscard]] std::size_t sampleCount() const noexcept;

private:
    struct Impl;
    explicit DeformRecording(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::physics
