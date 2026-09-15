#pragma once

/** @file ActionAudioBlock.h @brief Typed audio action-block payload contract. */

#include "action/ActionSpatialBlock.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::action {

/** @brief Authored audio block shape used to validate looping semantics. */
enum class ActionAudioShape : std::uint8_t { Instant, State };

/** @brief Owning validated settings shared by Audio runtime and editor preview. */
struct ActionAudioBinding {
    std::string          uri;
    std::vector<std::string> randomUris;
    ActionSpatialBinding spatial;
    double               volume = 1.0;
    double               pitch = 1.0;
    double               randomPitchOffset = 0.0;
    double               spatialBlend = 1.0;
    double               minDistance = 1.0;
    double               maxDistance = 35.0;
    bool                 looping = false;
    bool                 fadeOutOnExit = true;
    double               fadeOutDuration = 0.1;

    auto operator<=>(const ActionAudioBinding&) const = default;

    /**
     * @brief Decode an audio payload transactionally.
     * @param payload Owning dynamic payload borrowed only for this call.
     * @param shape Instant or state contract selected by the descriptor.
     * @return Validated owning settings or a field-path diagnostic.
     * @remarks Random URI entries must be non-empty and unique. Volume, pitch,
     * pitch variation, spatial blend, and attenuation distances use bounded,
     * finite ranges. Instant blocks cannot loop.
     */
    [[nodiscard]] static Result<ActionAudioBinding> fromPayload(const Value::Object& payload,
                                                                 ActionAudioShape shape);
};

}  // namespace eve::action
