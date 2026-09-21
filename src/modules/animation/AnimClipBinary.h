#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::animation {
class AnimClip;
class AnimSkeleton;
/** @brief Atomically replace a clip from the little-endian eve.animation-tracks/1 format (EVAC, version 1).
 * @param destination Borrowed existing clip; unchanged on rejection, owns decoded tracks on success.
 * @param bytes Borrowed complete encoded bytes, valid for this synchronous call only; never retained.
 * @param skeleton Borrowed bone-name mapping, valid for this call; never retained.
 * @return Applied or a diagnostic for invalid version, bounds, names, times, or values.
 * @thread Owner animation thread, outside sampling; no callbacks or reentrancy.
 * @details Unknown versions/flags/trailing bytes are rejected. No implicit migration. Float32 tracks
 * retain source sampling precision; the offline encoder only removes exactly constant keys.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<void> loadAnimationTracks(AnimClip&                  destination,
                                                                       std::span<const std::byte> bytes,
                                                                       const AnimSkeleton&        skeleton);

/** @brief One synchronous borrowed batch input; destination and bytes must outlive the call. */
struct AnimationTrackInput {
    std::reference_wrapper<AnimClip> destination;
    std::span<const std::byte>       bytes;
};
/** @brief Decode independent clips in parallel, then publish all or none in input order.
 * @param inputs Borrowed unique destinations and immutable bytes, at most 64 clips and 256 MiB total.
 * @param skeleton Borrowed immutable skeleton, used only until all workers join.
 * @param workerCount Zero selects up to eight hardware threads; 1 forces serial; valid range 0..8.
 * @return Applied with actual worker count, or diagnostic leaving every destination unchanged.
 * @thread Synchronous owner-thread entry, outside sampling/mutation. Workers touch only private
 * candidates and immutable inputs. No VM, filesystem, callbacks, retained jobs, or reentrancy.
 * @details Uses the same version-1 codec and exact sampling as loadAnimationTracks.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<int> loadAnimationTrackBatch(std::span<const AnimationTrackInput> inputs,
                                                                          const AnimSkeleton&                  skeleton,
                                                                          int workerCount = 0);
}  // namespace eve::animation
