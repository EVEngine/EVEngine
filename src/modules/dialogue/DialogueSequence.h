#pragma once
#include "common/Export.h"

/** @file DialogueSequence.h @brief Dialogue payload codecs for L1 sequence assets. */

#include "common/Result.h"
#include "common/StateAccess.h"
#include "dialogue/DialoguePayment.h"
#include "dialogue/DialogueState.h"
#include "dnut_interpreter/SequenceAsset.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "dnut_interpreter/StepKindRegistry.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dialogue {

/** @brief Read an optional string from a node payload; missing or ill-typed values yield empty. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION std::string sequencePayloadString(
    const eve::dnut::SequenceNode& node, std::string_view key);

/** @brief Read an optional string from a route payload; missing or ill-typed values yield empty. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION std::string sequenceRoutePayloadString(
    const eve::dnut::SequenceRoute& route, std::string_view key);

/** @brief Decode an optional dialogue payment from one canonical payload object. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<PaymentSpec> decodeSequencePayment(
    const eve::Value& payload);

/** @brief Decode dialogue state mutations from one canonical payload object. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<std::vector<eve::StateMutation>>
decodeSequenceStateMutations(const eve::Value& payload);

/** @brief Encode dialogue state mutations as the canonical payload array. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Value encodeSequenceStateMutations(
    std::span<const eve::StateMutation> mutations);

/** @brief Register dialogue's `line`, `choice`, and `command` contracts and validators. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<void> registerDialogueSequenceSteps(
    eve::dnut::StepKindRegistry& registry);

/** @brief Validate every dialogue-owned payload without taking control-flow ownership. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<void> validateDialogueSequenceAsset(
    const eve::dnut::SequenceAsset& asset, const eve::dnut::StepKindRegistry& registry);

/**
 * @brief Adapt an L1 command request to dialogue's transaction-bearing request.
 * @param request Owning L1 request snapshot; no data is retained.
 * @return Dialogue request with payment and mutations decoded from payload.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<CommandRequest>
decodeDialogueCommandRequest(const eve::dnut::SequenceCommandRequest& request);

}  // namespace eve::dialogue
