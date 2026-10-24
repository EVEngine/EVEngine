#pragma once
#include "common/Export.h"


#include "editing/EditingProtocol.h"
#include "editing/EditingTargetOperations.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace eve::editing {

/** @brief Final validation and commit boundary for editor mutations. */
class IEditAuthority {
public:
    /** @brief Releases IEditAuthority resources. */
    virtual ~IEditAuthority() = default;

    /** @brief Validate a transaction without mutating its target. */
    virtual Result<AuthorityPlan> preflight(const TransactionSpec&           transaction,
                                                  std::span<const DomainOperation> operations) = 0;
    /** @brief Atomically apply an authority-approved plan when possible. */
    virtual Result<TransactionReceipt> commit(const AuthorityPlan& plan) = 0;
    /** @brief Apply inverse operations as a new compensating action. */
    virtual Result<TransactionReceipt> compensate(const TransactionReceipt& receipt) = 0;
};

/**
 * @brief In-process authority for a single editable runtime or document target.
 *
 * The target is non-owning and must outlive the authority. Operations are
 * applied to a detached candidate and published once when the target implements
 * IDomainOperationTargetStaging. Failure preserves target state and revision.
 * Legacy targets without staging use inverse-operation compensation and cannot
 * promise strict atomicity. Use a staging target for automated atomic batches.
 * @thread Owner-thread-only; callers must serialize calls and must not re-enter
 * this authority from target lifecycle methods or publication callbacks.
 * @cost Candidate creation copies target state per transaction; batch related edits.
 */
class EVENGINE_API_PLATFORM LocalWorldAuthority final : public IEditAuthority {
public:
    /** @brief Bind a non-owning operation target. */
    explicit LocalWorldAuthority(IDomainOperationTarget* target) : target_(target) {}

    /** @brief Preflight. */
    Result<AuthorityPlan>      preflight(const TransactionSpec&           transaction,
                                               std::span<const DomainOperation> operations) override;
    /** @brief Commits commit. */
    Result<TransactionReceipt> commit(const AuthorityPlan& plan) override;
    /** @brief Compensate. */
    Result<TransactionReceipt> compensate(const TransactionReceipt& receipt) override;

private:
    struct CommittedEntry {
        TransactionReceipt           receipt;
        std::vector<DomainOperation> operations;
    };

    static DomainOperation inverseOf(const DomainOperation& operation);
    Result<void>     rollbackApplied(std::span<const DomainOperation> operations, std::size_t count);

    IDomainOperationTarget*                                                              target_ = nullptr;
    std::unordered_map<TransactionId, CommittedEntry, StrongIdHash<TransactionId>> committed_;
    std::vector<TransactionId>                                                     commitOrder_;
    std::uint64_t                                                                        receiptSequence_ = 0;
};

/** @brief Authority that permits discovery and dry-run but rejects commits. */
class EVENGINE_API_PLATFORM ReadOnlyAuthority final : public IEditAuthority {
public:
    /** @brief Preflight. */
    Result<AuthorityPlan>      preflight(const TransactionSpec&           transaction,
                                               std::span<const DomainOperation> operations) override;
    /** @brief Commits commit. */
    Result<TransactionReceipt> commit(const AuthorityPlan& plan) override;
    /** @brief Compensate. */
    Result<TransactionReceipt> compensate(const TransactionReceipt& receipt) override;
};

}  // namespace eve::editing
