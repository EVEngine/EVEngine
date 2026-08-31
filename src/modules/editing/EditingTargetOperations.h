#pragma once

#include "editing/EditingProtocol.h"
#include "editing/EditableTarget.h"

#include <memory>

namespace eve::editing {

/** @brief Editable-target capability that applies owning domain operations. */
class IDomainOperationTarget : public virtual IEditableTarget {
public:
    ~IDomainOperationTarget() override = default;

    /**
     * @brief Apply one validated domain operation.
     * @param operation Operation whose target must match this target.
     * @return Applied on mutation, otherwise a structured rejection or failure.
     * @remarks Implementations are main-thread-affine unless their domain contract states otherwise.
     */
    [[nodiscard]] virtual Result<void> applyDomainOperation(const DomainOperation& operation) = 0;
};

/** @brief Optional candidate/publish capability for atomic domain operations. */
class IDomainOperationTargetStaging {
public:
    virtual ~IDomainOperationTargetStaging() = default;

    /**
     * @brief Clone the complete target state into an owning detached candidate.
     * @return Owning candidate that shares no mutable state with the authoritative target.
     */
    [[nodiscard]] virtual std::unique_ptr<IDomainOperationTarget> cloneDomainState() const = 0;

    /**
     * @brief Atomically publish a validated candidate state.
     * @param candidate Owning candidate consumed by the publication attempt.
     * @return Applied when installed; on failure the authoritative target remains unchanged.
     */
    [[nodiscard]] virtual Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) = 0;
};

}  // namespace eve::editing
