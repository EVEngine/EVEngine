#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::animation {
/** @brief Owned scalar animation curves indexed by stable source and channel names.
 * @details Contains no skeleton, clip, filesystem or script references. Mutations
 * are owner-thread only, with no callbacks or reentrancy. Concurrent const sampling
 * is allowed while the owner guarantees no load/destruction. Samples use injected time.
 */
class EVENGINE_API_WORLD AnimCurveLibrary {
public:
    /** @brief Construct an empty library with independent lifetime. */
    AnimCurveLibrary();
    /** @brief Destroy owned curve data without accessing external objects. */
    ~AnimCurveLibrary();
    AnimCurveLibrary(const AnimCurveLibrary&)            = delete;
    AnimCurveLibrary& operator=(const AnimCurveLibrary&) = delete;
    /** @brief Atomically replace data from little-endian eve.animation-curves/1 bytes.
     * @param bytes Borrowed for this synchronous call only; decoded values are owned.
     * @return Applied, or InvalidArgument preserving all previous data. Rejects unknown
     * versions, trailing bytes, invalid UTF-8, duplicates, nonfinite/unsorted keys and
     * resource limits. Empty channels are absent; endpoints must span clip duration.
     */
    [[nodiscard]] eve::Result<void> load(std::span<const std::byte> bytes);
    /** @brief Query whether a source record exists, including records without channels. */
    [[nodiscard]] bool contains(std::string_view source) const;
    /** @brief Return the number of source records currently owned. */
    [[nodiscard]] std::size_t size() const noexcept;
    /** @brief Evaluate one scalar channel with linear interpolation.
     * @param source Borrowed stable source name; never retained.
     * @param channel Borrowed channel name; never retained.
     * @param seconds Finite simulation time; looping wraps negative time too, otherwise clamps.
     * @param loop Whether to wrap at the source duration. No external clip state is read.
     * @return Owning optional value (empty for an absent channel), NotFound for an
     * unknown source, or InvalidArgument for nonfinite time. No state is mutated;
     * repeat calls are deterministic within float rounding tolerance.
     */
    [[nodiscard]] eve::Result<std::optional<float>> sample(std::string_view source, std::string_view channel,
                                                           double seconds, bool loop) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::animation
