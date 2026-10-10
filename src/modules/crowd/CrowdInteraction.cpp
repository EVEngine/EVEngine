#include "crowd/CrowdInternal.h"

namespace eve::crowd {

Result<void> Crowd::setAgentInteraction(int id, AgentInteraction policy) {
    if (!impl_->validId(id))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "id"));
    if (!std::isfinite(policy.pushability) || policy.pushability < 0.f || policy.pushability > 1.f ||
        policy.layer < 0 || policy.mask < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid crowd interaction policy", "policy"));
    const size_t index         = size_t(id);
    impl_->interactions[index] = policy;
    if (policy.holdPosition) {
        impl_->vxs[index] = impl_->vys[index] = impl_->speeds[index] = 0.f;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<AgentInteraction> Crowd::getAgentInteraction(int id) const {
    if (!impl_->validId(id))
        return Result<AgentInteraction>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "id"));
    return Result<AgentInteraction>::success(impl_->interactions[size_t(id)]);
}

}  // namespace eve::crowd
