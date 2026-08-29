#pragma once

#include "editor/EditorCommandTypes.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorResult.h"
#include "editor/EditorValue.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::editor {

using Revision = std::uint64_t;

/** @brief Stable reference to an object owned by an editable target. */
struct ObjectRefValue {
    TargetId      target;
    std::string   object;
    std::uint64_t generation = 0;

    auto operator<=>(const ObjectRefValue&) const = default;
};

/** @brief Immutable context captured when an editor request is created. */
struct EditorContextSnapshot {
    SessionId                session;
    HostKind                 host = HostKind::Developer;
    TargetId                 target;
    Revision                 targetRevision = 0;
    std::vector<std::string> selection;
    std::vector<std::string> inputContexts;
};

/** @brief Serializable business mutation produced by an editor command. */
struct DomainOperation {
    std::string                 type;
    std::string                 inverseType;
    TargetId                    target;
    EditorValue                 payload;
    EditorValue                 inverse;
    bool                        hasInverse = false;
    std::vector<ObjectRefValue> affectedObjects;
    std::vector<std::string>    affectedProperties;
    std::string                 mergeKey;
};

/** @brief Request passed to a plan-aware editor command handler. */
struct CommandRequest {
    CommandId               id;
    EditorValue             payload;
    CommandSource           source = CommandSource::Api;
    EditorContextSnapshot   context;
    std::optional<Revision> expectedRevision;
    bool                    dryRun = false;
};

/** @brief Side-effect-free plan returned before command execution. */
struct CommandPlan {
    PlanId                        id;
    CommandId                     command;
    TargetId                      target;
    Revision                      baseRevision = 0;
    std::vector<DomainOperation>  operations;
    EditorValue                   summary;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Lifecycle state of an editor transaction. */
enum class TransactionState {
    Planning,
    Previewing,
    PendingAuthority,
    Committed,
    Rejected,
    Conflicted,
    RolledBack,
    Failed
};

/** @brief Origin used for history, policy and telemetry. */
enum class ActionOrigin { User, Game, Script, Automation, Importer, Network };

/** @brief Metadata supplied when a transaction begins. */
struct TransactionSpec {
    TransactionId id;
    std::string   label;
    ActionOrigin  origin = ActionOrigin::User;
    TargetId      target;
    Revision      baseRevision = 0;
    std::string   mergeKey;
    bool          restoreSelection = true;
};

/** @brief Durable outcome of an authority commit or compensation. */
struct TransactionReceipt {
    TransactionId                 id;
    TransactionState              state          = TransactionState::Failed;
    Revision                      beforeRevision = 0;
    Revision                      afterRevision  = 0;
    std::vector<ObjectRefValue>   affectedObjects;
    std::vector<EditorDiagnostic> diagnostics;
    std::string                   authorityReceipt;
};

/** @brief Authority-approved immutable commit input. */
struct AuthorityPlan {
    TransactionSpec              transaction;
    Revision                     validatedRevision = 0;
    std::vector<DomainOperation> operations;
};

}  // namespace eve::editor
