#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "editing/EditingCommandTypes.h"
#include "editing/EditingIds.h"
#include "editing/EditingResult.h"
#include "editing/EditingValue.h"
namespace eve::editing {
using Revision = std::uint64_t;
enum class HostKind { Developer, RuntimeBuilder, RuntimeAdmin, Automation };
struct ObjectRefValue {
    TargetId      target;
    std::string   object;
    std::uint64_t generation                               = 0;
    auto          operator<=>(const ObjectRefValue&) const = default;
};
struct ContextSnapshot {
    SessionId                session;
    HostKind                 host = HostKind::Developer;
    TargetId                 target;
    Revision                 targetRevision = 0;
    std::vector<std::string> selection;
    std::vector<std::string> inputContexts;
};
struct DomainOperation {
    std::string                 type;
    std::string                 inverseType;
    TargetId                    target;
    Value                       payload;
    Value                       inverse;
    bool                        hasInverse = false;
    std::vector<ObjectRefValue> affectedObjects;
    std::vector<std::string>    affectedProperties;
    std::string                 mergeKey;
};
struct CommandRequest {
    CommandId               id;
    Value                   payload;
    CommandSource           source = CommandSource::Api;
    ContextSnapshot         context;
    std::optional<Revision> expectedRevision;
    bool                    dryRun = false;
};
struct CommandPlan {
    PlanId                       id;
    CommandId                    command;
    TargetId                     target;
    Revision                     baseRevision = 0;
    std::vector<DomainOperation> operations;
    Value                        summary;
    std::vector<Diagnostic>      diagnostics;
};
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
enum class ActionOrigin { User, Game, Script, Automation, Importer, Network };
struct TransactionSpec {
    TransactionId id;
    std::string   label;
    ActionOrigin  origin = ActionOrigin::User;
    TargetId      target;
    Revision      baseRevision = 0;
    std::string   mergeKey;
    bool          restoreSelection = true;
};
struct TransactionReceipt {
    TransactionId               id;
    TransactionState            state          = TransactionState::Failed;
    Revision                    beforeRevision = 0;
    Revision                    afterRevision  = 0;
    std::vector<ObjectRefValue> affectedObjects;
    std::vector<Diagnostic>     diagnostics;
    std::string                 authorityReceipt;
};
struct AuthorityPlan {
    TransactionSpec              transaction;
    Revision                     validatedRevision = 0;
    std::vector<DomainOperation> operations;
};
}  // namespace eve::editing
