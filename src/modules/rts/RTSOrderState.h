#pragma once
/** @file RTSOrderState.h
 * @brief rts module-interface: portable generic queue plus RTS command/target correspondence.
 * @remarks Data face rts.orders v1; no services, ECS mutation, events, scripts or tools.
 */
#include "rts/RTSTypes.h"
namespace eve::rts {
/** @brief Detached transfer value; the canonical queue remains the live order owner.
 * @ownership Owns queue text, command fields and stable target identities; no world or provider retained.
 */
struct RTSOrderState {
    OrderComponent::Snapshot          orders;
    std::map<std::string, SubjectRef> targets;
    /** @brief Encode complete canonical queue/history and portable commands, rejecting native targets.
     * @return rts.orders v1 or canonical queue/command/correspondence errors.
     * @thread Read-only owner-thread boundary; no callbacks, ECS or services.
     * @cost Copies and validates the full queue and command map once per explicit save.
     * @throws std::bad_alloc Input remains unchanged.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<Value> encodeState() const;
    /** @brief Admit strict v1 values through the same queue/extension validator as native restore.
     * @return Detached transfer facts, or unknown schema/version/fields, malformed values or broken links.
     * @remarks Initial v1 has no migration. Does not resolve targets or authorize entity creation.
     * @thread Pure CPU boundary; no callbacks or services. Returned facts outlive input destruction.
     * @cost Copies and validates all queue records, events and extensions once per explicit load.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static EVENGINE_API_DOMAINS Result<RTSOrderState> prepareState(const Value& value);
};
}  // namespace eve::rts
