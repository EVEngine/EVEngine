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
/** @brief HostKind public API. */
enum class HostKind { Developer, RuntimeBuilder, RuntimeAdmin, Automation };
/** @brief ObjectRefValue public API. */
struct ObjectRefValue {
    TargetId      target;
    ObjectId      object;
    std::uint64_t generation = 0;
    /** @brief Constructs a ObjectRefValue. */
    ObjectRefValue()         = default;
    /** @brief Constructs a ObjectRefValue. */
    ObjectRefValue(TargetId targetId, ObjectId objectId, std::uint64_t objectGeneration = 0)
        /** @brief Target. */
        : target(std::move(targetId)), object(std::move(objectId)), generation(objectGeneration) {}
    /** @brief Constructs a ObjectRefValue. */
    ObjectRefValue(TargetId targetId, std::string objectId, std::uint64_t objectGeneration = 0)
        /** @brief Target. */
        : target(std::move(targetId)), object(ObjectId(std::move(objectId))), generation(objectGeneration) {}
    /** @brief Operator <=>. */
    auto operator<=>(const ObjectRefValue&) const = default;
};
/** @brief ContextSnapshot public API. */
struct ContextSnapshot {
    SessionId                session;
    HostKind                 host = HostKind::Developer;
    TargetId                 target;
    Revision                 targetRevision = 0;
    /** @brief Monotonic lifetime generation of the bound target instance. */
    std::uint64_t            targetGeneration = 0;
    std::vector<ObjectId>    selection;
    std::vector<std::string> inputContexts;
};
/** @brief DomainOperation public API. */
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
/** @brief CommandRequest public API. */
struct CommandRequest {
    CommandId               id;
    Value                   payload;
    CommandSource           source = CommandSource::Api;
    ContextSnapshot         context;
    std::optional<Revision> expectedRevision;
    bool                    dryRun = false;
};
/** @brief CommandPlan public API. */
struct CommandPlan {
    PlanId                       id;
    CommandId                    command;
    TargetId                     target;
    Revision                     baseRevision = 0;
    /** @brief Target lifetime generation captured while planning. */
    std::uint64_t                targetGeneration = 0;
    /** @brief Command registration generation captured while planning. */
    std::uint64_t                commandGeneration = 0;
    /** @brief Immutable payload authorized by this plan. */
    Value                        plannedPayload;
    /** @brief Invocation source authorized by this plan. */
    CommandSource                plannedSource = CommandSource::Api;
    std::vector<DomainOperation> operations;
    Value                        summary;
    std::vector<Diagnostic>      diagnostics;
};
/** @brief TransactionState public API. */
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
/** @brief ActionOrigin public API. */
enum class ActionOrigin { User, Game, Script, Automation, Importer, Network };
/** @brief TransactionSpec public API. */
struct TransactionSpec {
    TransactionId id;
    std::string   label;
    ActionOrigin  origin = ActionOrigin::User;
    TargetId      target;
    Revision      baseRevision = 0;
    std::string   mergeKey;
    bool          restoreSelection = true;
};
/** @brief TransactionReceipt public API. */
struct TransactionReceipt {
    TransactionId               id;
    TransactionState            state          = TransactionState::Failed;
    Revision                    beforeRevision = 0;
    Revision                    afterRevision  = 0;
    std::vector<ObjectRefValue> affectedObjects;
    std::vector<Diagnostic>     diagnostics;
    std::string                 authorityReceipt;
};
/** @brief AuthorityPlan public API. */
struct AuthorityPlan {
    TransactionSpec              transaction;
    Revision                     validatedRevision = 0;
    std::vector<DomainOperation> operations;
};
}  // namespace eve::editing
