#pragma once

/** @file ActionComboWindowState.h @brief Semantic input matching for active Montage combo windows. */

#include "action/ActionStateWindowBlock.h"
#include "common/SubjectRef.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eve::action::input {

/**
 * @brief Resolves an owning stable subject from a generation-qualified ECS handle.
 * @remarks Called synchronously on the owner thread without locks. It must not
 * throw, reenter this state owner, or retain the handle.
 */
using ComboSubjectResolver = std::function<Result<SubjectRef>(ecs::EntityHandle)>;

/** @brief Owning deterministic match between one semantic input and an active combo window. */
struct ActionComboMatch {
    ActionExecutionId executionId;
    LogicalId         itemId;
    SubjectRef        subject;
    std::string       input;
};

/**
 * @brief Authoritative projection of active semantic combo-input windows.
 *
 * This object does not read devices or submit gameplay commands. Player, AI, and
 * script input adapters map their source into the same semantic input string and
 * query this owner before choosing the next action. All methods are owner-thread-only.
 */
class ActionComboWindowState final : public IActionStateWindowSink {
public:
    /** @brief Construct with a synchronous stable-subject resolver. */
    explicit ActionComboWindowState(ComboSubjectResolver resolver);
    ~ActionComboWindowState() override;

    /** @brief Opt this state owner into or out of Action window dispatch. */
    void setEnabled(bool enabled);
    /** @brief Return whether this exact owner is registered. */
    [[nodiscard]] bool enabled() const;
    /** @copydoc IActionStateWindowSink::supports */
    [[nodiscard]] bool supports(ActionStateWindowKind kind) const noexcept override;
    /** @copydoc IActionStateWindowSink::enter */
    [[nodiscard]] Result<void> enter(const ActionStateWindowBinding& binding,
                                     const ActionTimelineEvent& event,
                                     const ActionNotifyContext& context) override;
    /** @copydoc IActionStateWindowSink::exit */
    [[nodiscard]] Result<void> exit(const ActionStateWindowBinding& binding,
                                    const ActionTimelineEvent& event,
                                    const ActionNotifyContext& context) override;

    /** @brief Return sorted unique semantic inputs currently accepted for a subject. */
    [[nodiscard]] std::vector<std::string> availableInputs(SubjectRef subject) const;
    /** @brief Resolve the first deterministic active window accepting an input. */
    [[nodiscard]] Result<ActionComboMatch> match(SubjectRef subject, std::string_view input) const;
    /** @brief Return the number of active execution/item keys. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_.size(); }

private:
    using ActiveKey = std::pair<ActionExecutionId, std::string>;
    struct ActiveCombo {
        LogicalId  itemId;
        SubjectRef subject;
        std::string input;
    };

    ComboSubjectResolver             resolver_;
    std::map<ActiveKey, ActiveCombo> active_;
};

}  // namespace eve::action::input
