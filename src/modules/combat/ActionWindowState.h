#pragma once

/** @file ActionWindowState.h @brief Combat-owned active hitbox and invulnerability projections. */

#include "action/ActionStateWindowBlock.h"
#include "common/SubjectRef.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eve::combat {

/**
 * @brief Resolves an owning stable subject from a generation-qualified ECS handle.
 * @remarks Called synchronously on the owner thread without locks. It must not
 * throw, reenter the state owner, or retain the handle.
 */
using ActionWindowSubjectResolver = std::function<Result<SubjectRef>(ecs::EntityHandle)>;

/**
 * @brief Authoritative combat projection of active Montage hitbox and invulnerability windows.
 *
 * Keys combine ActionExecutionId and timeline item identity, so overlapping windows
 * compose without a lossy boolean. Exit uses the retained owning SubjectRef and is
 * safe when the ECS entity was destroyed after enter. This object is owner-thread-only.
 */
class CombatActionWindowState final : public action::IActionStateWindowSink {
public:
    /** @brief Construct with a synchronous resolver copied into the state owner. */
    explicit CombatActionWindowState(ActionWindowSubjectResolver resolver);
    ~CombatActionWindowState() override;

    /** @brief Opt this owner into or out of Action state-window dispatch. */
    void setEnabled(bool enabled);
    /** @brief Return whether this exact owner is registered. */
    [[nodiscard]] bool enabled() const;
    /** @copydoc action::IActionStateWindowSink::supports */
    [[nodiscard]] bool supports(action::ActionStateWindowKind kind) const noexcept override;
    /** @copydoc action::IActionStateWindowSink::enter */
    [[nodiscard]] Result<void> enter(const action::ActionStateWindowBinding& binding,
                                     const action::ActionTimelineEvent& event,
                                     const action::ActionNotifyContext& context) override;
    /** @copydoc action::IActionStateWindowSink::exit */
    [[nodiscard]] Result<void> exit(const action::ActionStateWindowBinding& binding,
                                    const action::ActionTimelineEvent& event,
                                    const action::ActionNotifyContext& context) override;

    /** @brief Return whether at least one active Montage window protects this stable subject. */
    [[nodiscard]] bool isInvulnerable(SubjectRef subject) const noexcept;
    /** @brief Return owning hitbox identifiers active for a stable subject in key order. */
    [[nodiscard]] std::vector<std::string> activeHitboxes(SubjectRef subject) const;
    /** @brief Return the total number of retained active window keys. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_.size(); }

private:
    using ActiveKey = std::pair<action::ActionExecutionId, std::string>;
    struct ActiveWindow {
        action::ActionStateWindowBinding binding;
        SubjectRef                       subject;
    };

    ActionWindowSubjectResolver       resolver_;
    std::map<ActiveKey, ActiveWindow> active_;
};

}  // namespace eve::combat
