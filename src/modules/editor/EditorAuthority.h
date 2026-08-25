#pragma once

#include "editor/EditorProtocol.h"
#include "editor/EditorTarget.h"

#include <span>
#include <unordered_map>

namespace eve::editor {

/** @brief Target capability that applies serializable domain operations. */
class IDomainOperationTarget : public virtual IEditableTarget {
public:
    ~IDomainOperationTarget() override = default;

    /**
     * @brief Apply one validated domain operation.
     * @param operation Operation whose target must match this target.
     * @return Applied on mutation, otherwise a structured rejection/failure.
     */
    virtual EditorResult<void> applyDomainOperation(const DomainOperation& operation) = 0;
};

/** @brief Final validation and commit boundary for editor mutations. */
class IEditAuthority {
public:
    virtual ~IEditAuthority() = default;

    /** @brief Validate a transaction without mutating its target. */
    virtual EditorResult<AuthorityPlan> preflight(const TransactionSpec&           transaction,
                                                  std::span<const DomainOperation> operations) = 0;
    /** @brief Atomically apply an authority-approved plan when possible. */
    virtual EditorResult<TransactionReceipt> commit(const AuthorityPlan& plan) = 0;
    /** @brief Apply inverse operations as a new compensating action. */
    virtual EditorResult<TransactionReceipt> compensate(const TransactionReceipt& receipt) = 0;
};

/**
 * @brief In-process authority for a single editable runtime or document target.
 *
 * The target is non-owning and must outlive the authority. Operations are
 * rolled back in reverse order if a later operation fails.
 */
class LocalWorldAuthority final : public IEditAuthority {
public:
    /** @brief Bind a non-owning operation target. */
    explicit LocalWorldAuthority(IDomainOperationTarget* target) : target_(target) {}

    EditorResult<AuthorityPlan>      preflight(const TransactionSpec&           transaction,
                                               std::span<const DomainOperation> operations) override;
    EditorResult<TransactionReceipt> commit(const AuthorityPlan& plan) override;
    EditorResult<TransactionReceipt> compensate(const TransactionReceipt& receipt) override;

private:
    struct CommittedEntry {
        TransactionReceipt           receipt;
        std::vector<DomainOperation> operations;
    };

    static DomainOperation inverseOf(const DomainOperation& operation);
    EditorResult<void>     rollbackApplied(std::span<const DomainOperation> operations, std::size_t count);

    IDomainOperationTarget*                                                              target_ = nullptr;
    std::unordered_map<TransactionId, CommittedEntry, StrongEditorIdHash<TransactionId>> committed_;
    std::uint64_t                                                                        receiptSequence_ = 0;
};

/** @brief Authority that permits discovery and dry-run but rejects commits. */
class ReadOnlyAuthority final : public IEditAuthority {
public:
    EditorResult<AuthorityPlan>      preflight(const TransactionSpec&           transaction,
                                               std::span<const DomainOperation> operations) override;
    EditorResult<TransactionReceipt> commit(const AuthorityPlan& plan) override;
    EditorResult<TransactionReceipt> compensate(const TransactionReceipt& receipt) override;
};

}  // namespace eve::editor
