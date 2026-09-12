#pragma once

/** @file ActionAudioWaveform.h @brief Optional audio waveform projection for action editors. */

#include "action/ActionAudioBlock.h"
#include "common/Time.h"

#include <cstddef>
#include <vector>

namespace eve::action {

/** @brief One normalized min/max envelope bucket in timeline display order. */
struct ActionAudioWaveformBucket {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

/** @brief Owning request for a bounded audio-block waveform projection. */
struct ActionAudioWaveformRequest {
    /** @brief Validated owning audio settings; only URI, pitch and looping affect the projection. */
    ActionAudioBinding binding;
    /** @brief Authored timeline span represented by the block. */
    Duration           blockDuration;
    /** @brief Requested bounded horizontal resolution. */
    std::size_t        bucketCount = 0;
};

/** @brief Owning waveform projection returned by an optional audio provider. */
struct ActionAudioWaveform {
    /** @brief Decoded source duration used for diagnostics and host labels. */
    double                                 clipDurationSeconds = 0.0;
    /** @brief Min/max buckets in increasing block-local time. */
    std::vector<ActionAudioWaveformBucket> buckets;
};

/**
 * @brief Optional synchronous audio waveform service consumed by the action editor.
 *
 * The provider owns decoded resources and returns an independent bounded value.
 * Calls are owner-thread-only and must not retain the borrowed request. Module
 * shutdown must revoke the capability before destroying the implementation.
 */
class IActionAudioWaveformProvider {
public:
    static constexpr const char* capabilityName = "IActionAudioWaveformProvider";
    virtual ~IActionAudioWaveformProvider() = default;

    /**
     * @brief Decode and project one validated audio block into display buckets.
     * @param request Borrowed request valid only for this synchronous call.
     * @return Owning waveform, or a structured resource/format diagnostic.
     */
    [[nodiscard]] virtual Result<ActionAudioWaveform> waveform(
        const ActionAudioWaveformRequest& request) = 0;
};

}  // namespace eve::action
