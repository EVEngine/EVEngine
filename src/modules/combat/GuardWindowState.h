#pragma once
#include "common/Export.h"

/** @file GuardWindowState.h @brief Block and parry windows projected from Montage state. */

#include "action/ActionStateWindowBlock.h"
#include "combat/Damage.h"
#include "common/SubjectRef.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eve::combat {

/** @brief Guard mode authored on a combat:guard-window timeline item. */
enum class GuardMode : std::uint8_t { Block, Parry };

/** @brief How an incoming hit interacts with an active guard window. */
enum class GuardResult : std::uint8_t { None, Blocked, Parried };

/** @brief Owning audit of one guard evaluation. */
struct GuardEvaluation {
    GuardResult result = GuardResult::None;
    SubjectRef  defender;
    SubjectRef  attacker;
    GuardMode   mode = GuardMode::Block;
};

/**
 * @brief Resolves an owning stable subject from a generation-qualified ECS handle.
 */
using GuardSubjectResolver = std::function<Result<SubjectRef>(ecs::EntityHandle)>;

/**
 * @brief Authoritative projection of active block/parry Montage windows.
 *
 * Resource strings are `block` or `parry`. Overlapping windows compose by key;
 * evaluation prefers Parry over Block when both are active for a defender.
 */
class EVENGINE_API_BACKENDS GuardWindowState final : public action::IActionStateWindowSink {
public:
    /** @brief Construct with a synchronous subject resolver. */
    explicit GuardWindowState(GuardSubjectResolver resolver);
    /** @brief Guard window state. */
    ~GuardWindowState() override;

    /** @brief Opt this owner into or out of Action state-window dispatch. */
    void setEnabled(bool enabled);
    /** @brief Return whether this exact owner is registered. */
    [[nodiscard]] bool enabled() const;
    /** @copydoc action::IActionStateWindowSink::supports */
    /** @brief Supports. */
    [[nodiscard]] bool supports(action::ActionStateWindowKind kind) const noexcept override;
    /** @copydoc action::IActionStateWindowSink::enter */
    /** @brief Enter. */
    [[nodiscard]] Result<void> enter(const action::ActionStateWindowBinding& binding,
                                     const action::ActionTimelineEvent& event,
                                     const action::ActionNotifyContext& context) override;
    /** @copydoc action::IActionStateWindowSink::exit */
    /** @brief Exit. */
    [[nodiscard]] Result<void> exit(const action::ActionStateWindowBinding& binding,
                                    const action::ActionTimelineEvent& event,
                                    const action::ActionNotifyContext& context) override;

    /** @brief Evaluate whether a defender currently blocks or parries an attacker. */
    [[nodiscard]] Result<GuardEvaluation> evaluate(SubjectRef defender, SubjectRef attacker) const;
    /**
     * @brief Mutate a damage request when guard succeeds.
     * @remarks Block zeros health/poise and knockback. Parry zeros damage and tags reaction None.
     */
    [[nodiscard]] Result<GuardEvaluation> mitigate(SubjectRef defender, DamageRequest& request) const;
    /** @brief Return active guard modes for a subject in key order. */
    [[nodiscard]] std::vector<GuardMode> activeModes(SubjectRef subject) const;
    /** @brief Number of retained active window keys. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_.size(); }

private:
    using ActiveKey = std::pair<action::ActionExecutionId, std::string>;
    struct ActiveGuard {
        action::ActionStateWindowBinding binding;
        SubjectRef                       subject;
        GuardMode                        mode = GuardMode::Block;
    };

    GuardSubjectResolver            resolver_;
    std::map<ActiveKey, ActiveGuard> active_;
};

}  // namespace eve::combat
