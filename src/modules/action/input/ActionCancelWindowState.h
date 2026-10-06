#pragma once
#include "common/Export.h"

/** @file ActionCancelWindowState.h @brief Cancel-window projection for action combat. */

#include "action/ActionStateWindowBlock.h"
#include "common/SubjectRef.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eve::action::input {

/** @brief Resolves an owning stable subject from a generation-qualified ECS handle. */
using CancelSubjectResolver = std::function<Result<SubjectRef>(ecs::EntityHandle)>;

/** @brief Owning cancel match between an input/ability tag and an active window. */
struct ActionCancelMatch {
    ActionExecutionId executionId;
    LogicalId         itemId;
    SubjectRef        subject;
    std::string       allowed;
    std::int32_t      priority = 0;
};

/**
 * @brief Authoritative projection of active cancel windows.
 *
 * Timeline resource is a semicolon-separated allow-list (e.g. `dodge;heavy-attack`).
 * Optional payload field `priority` is parsed into ActionStateWindowBinding::priority.
 */
class EVENGINE_API_BACKENDS ActionCancelWindowState final : public IActionStateWindowSink {
public:
    /** @brief Construct with a synchronous stable-subject resolver. */
    explicit ActionCancelWindowState(CancelSubjectResolver resolver);
    ~ActionCancelWindowState() override;

    void setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool supports(ActionStateWindowKind kind) const noexcept override;
    [[nodiscard]] Result<void> enter(const ActionStateWindowBinding& binding,
                                     const ActionTimelineEvent& event,
                                     const ActionNotifyContext& context) override;
    [[nodiscard]] Result<void> exit(const ActionStateWindowBinding& binding,
                                    const ActionTimelineEvent& event,
                                    const ActionNotifyContext& context) override;

    /** @brief Return sorted unique allow tags currently active for a subject. */
    [[nodiscard]] std::vector<std::string> availableCancels(SubjectRef subject) const;
    /** @brief Resolve the highest-priority window that allows the semantic input. */
    [[nodiscard]] Result<ActionCancelMatch> match(SubjectRef subject, std::string_view input) const;
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_.size(); }

private:
    using ActiveKey = std::pair<ActionExecutionId, std::string>;
    struct ActiveCancel {
        LogicalId                itemId;
        SubjectRef               subject;
        std::vector<std::string> allows;
        std::int32_t             priority = 0;
    };

    CancelSubjectResolver             resolver_;
    std::map<ActiveKey, ActiveCancel> active_;
};

}  // namespace eve::action::input
