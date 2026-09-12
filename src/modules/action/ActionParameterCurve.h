#pragma once

/** @file ActionParameterCurve.h @brief Typed continuous parameter-curve action block. */

#include "action/ActionNotifyRegistry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::action {

/** @brief Interpolation used from one parameter key to the following key. */
enum class ActionParameterInterpolation : std::uint8_t { Step, Linear, Cubic };

/** @brief Composition operation applied by a target-specific parameter sink. */
enum class ActionParameterOperation : std::uint8_t { Replace, Add, Multiply };

/** @brief One normalized, owning parameter-curve key. */
struct ActionParameterKey {
    /** @brief Normalized key time in [0, 1]. */
    double                       time = 0.0;
    /** @brief Authored scalar value. */
    double                       value = 0.0;
    /** @brief Incoming value-per-normalized-time tangent for cubic interpolation. */
    double                       inTangent = 0.0;
    /** @brief Outgoing value-per-normalized-time tangent for cubic interpolation. */
    double                       outTangent = 0.0;
    /** @brief Interpolation from this key to the following key. */
    ActionParameterInterpolation interpolation = ActionParameterInterpolation::Linear;

    auto operator<=>(const ActionParameterKey&) const = default;
};

/** @brief Validated owning continuous parameter binding. */
struct ActionParameterCurveBinding {
    /** @brief Stable sink-owned parameter target. */
    LogicalId                      target;
    /** @brief Composition operation applied at the target. */
    ActionParameterOperation       operation = ActionParameterOperation::Replace;
    /** @brief Strictly ordered normalized keys, including endpoints 0 and 1. */
    std::vector<ActionParameterKey> keys;

    auto operator<=>(const ActionParameterCurveBinding&) const = default;

    /** @brief Decode and validate target, operation and normalized ordered keys. */
    [[nodiscard]] static Result<ActionParameterCurveBinding> fromPayload(const Value::Object& payload);
    /** @brief Encode the canonical fields while preserving unrelated extension fields. */
    [[nodiscard]] Value::Object toPayload(Value::Object extensions = {}) const;
    /** @brief Evaluate the curve at normalized block progress in [0, 1]. */
    [[nodiscard]] double sample(double progress) const noexcept;
};

/** @brief Stable persisted interpolation spelling. */
[[nodiscard]] std::string_view actionParameterInterpolationName(ActionParameterInterpolation value) noexcept;
/** @brief Parse a stable persisted interpolation spelling. */
[[nodiscard]] std::optional<ActionParameterInterpolation> actionParameterInterpolationFromName(
    std::string_view value) noexcept;
/** @brief Stable persisted operation spelling. */
[[nodiscard]] std::string_view actionParameterOperationName(ActionParameterOperation value) noexcept;

/** @brief Parameter lifecycle projected to a target-specific optional sink. */
enum class ActionParameterPhase : std::uint8_t { Begin, Update, End };

/** @brief Owning continuous parameter sample emitted by ActionBlockRuntime. */
struct ActionParameterSample {
    /** @brief Paired lifecycle phase. */
    ActionParameterPhase     phase = ActionParameterPhase::Update;
    /** @brief Generation-qualified action execution identity. */
    ActionExecutionId        executionId;
    /** @brief Stable timeline item identity within the execution. */
    LogicalId                itemId;
    /** @brief Stable sink-owned parameter target. */
    LogicalId                target;
    /** @brief Composition operation for this curve. */
    ActionParameterOperation operation = ActionParameterOperation::Replace;
    /** @brief Evaluated scalar value. */
    double                   value = 0.0;
    /** @brief Whether the sample belongs to non-authoritative preview evaluation. */
    bool                     preview = false;
};

/**
 * @brief Optional open target for continuous action parameters.
 *
 * Implementations own target state and must make Begin/Update/End pairing
 * deterministic for generation-qualified execution/item identity. Calls are
 * owner-thread-only and must not retain the borrowed sample.
 */
class IActionParameterSink {
public:
    static constexpr const char* capabilityName = "IActionParameterSink";
    virtual ~IActionParameterSink() = default;

    /** @brief Whether this sink owns the supplied stable target. */
    [[nodiscard]] virtual bool supports(const LogicalId& target) const noexcept = 0;
    /** @brief Apply one borrowed lifecycle sample and report any target failure. */
    [[nodiscard]] virtual Result<void> apply(const ActionParameterSample& sample) = 0;
};

}  // namespace eve::action
