#pragma once

/** @file ActionAudioBlock.h @brief Typed audio action-block payload contract. */

#include "action/ActionSpatialBlock.h"

#include <cstdint>
#include <string>

namespace eve::action {

/** @brief Authored audio block shape used to validate looping semantics. */
enum class ActionAudioShape : std::uint8_t { Instant, State };

/** @brief Owning validated settings shared by Audio runtime and editor preview. */
struct ActionAudioBinding {
    std::string          uri;
    ActionSpatialBinding spatial;
    double               volume = 1.0;
    double               pitch = 1.0;
    bool                 looping = false;

    auto operator<=>(const ActionAudioBinding&) const = default;

    /**
     * @brief Decode an audio payload transactionally.
     * @param payload Owning dynamic payload borrowed only for this call.
     * @param shape Instant or state contract selected by the descriptor.
     * @return Validated owning settings or a field-path diagnostic.
     * @remarks Volume must be finite and non-negative; pitch must be finite
     * and positive. Instant blocks cannot loop.
     */
    [[nodiscard]] static Result<ActionAudioBinding> fromPayload(const Value::Object& payload,
                                                                 ActionAudioShape shape);
};

}  // namespace eve::action
