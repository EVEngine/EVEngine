#pragma once

/** @file ActionGameEventSink.h @brief Persistent GameEventLog adapter for action events. */

#include "action/ActionGameplayEventBlock.h"
#include "common/SubjectRef.h"
#include "game_event/GameEvent.h"

#include <functional>
#include <optional>

namespace eve::action::game_event_adapter {

/**
 * @brief Resolves a stable gameplay subject for a generation-qualified ECS handle.
 * @remarks Called synchronously on the owner thread without locks. It must not
 * throw, reenter the adapter, or retain the handle. The result is owning.
 */
using ActionSubjectResolver = std::function<Result<SubjectRef>(ecs::EntityHandle)>;

/**
 * @brief Explicit bridge from gameplay:event blocks to one persistent GameEventLog.
 *
 * The caller owns the log and subject resolver. This adapter owns no event-domain
 * state; the log remains the only ordering, identity, persistence, and restore authority.
 */
class ActionGameEventSink final : public IActionGameplayEventSink {
public:
    /**
     * @brief Construct from borrowed event authority and an owning resolver callback.
     * @param log Log that must outlive this adapter.
     * @param resolver Synchronous stable-subject resolver copied into the adapter.
     * @lifetime The adapter retains a raw pointer to log until destruction.
     */
    ActionGameEventSink(eve::game_event::GameEventLog& log, ActionSubjectResolver resolver);
    ~ActionGameEventSink() override;

    /** @brief Opt this adapter into or out of gameplay-event dispatch. */
    void setEnabled(bool enabled);
    /** @brief Return whether this exact adapter is registered. */
    [[nodiscard]] bool enabled() const;
    /** @copydoc IActionGameplayEventSink::emit */
    [[nodiscard]] Result<void> emit(const ActionGameplayEventBinding& binding,
                                    const ActionNotifyContext& context) override;
    /** @brief Return the last successfully appended stream-local sequence. */
    [[nodiscard]] std::optional<eve::game_event::EventSequence> lastSequence() const noexcept;

private:
    eve::game_event::GameEventLog*                 log_ = nullptr;
    ActionSubjectResolver                          resolver_;
    std::optional<eve::game_event::EventSequence> lastSequence_;
};

}  // namespace eve::action::game_event_adapter
