#include "crowd/CrowdInternal.h"

namespace eve::crowd {

InteractionPolicy canonicalInteraction(AgentInteraction policy) {
    return {policy.pushability, policy.holdPosition, policy.layer, policy.mask, policy.layer, policy.mask};
}

bool validInteraction(InteractionPolicy policy) {
    return std::isfinite(policy.pushability) && policy.pushability >= 0.f && policy.pushability <= 1.f &&
           policy.avoidanceLayer >= 0 && policy.avoidanceMask >= 0 && policy.contactLayer >= 0 &&
           policy.contactMask >= 0;
}

Result<void> Crowd::setAgentInteraction(int id, AgentInteraction policy) {
    auto reference = agentRef(id);
    if (!reference) return Result<void>::failure(reference.status());
    return setInteractionPolicy(reference.value(), canonicalInteraction(policy));
}

Result<void> Crowd::setInteractionPolicy(AgentRef reference, InteractionPolicy policy) {
    const int slot = impl_->resolveAgent(reference);
    if (slot < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "reference"));
    if (!validInteraction(policy))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid crowd interaction policy", "policy"));
    const size_t index         = size_t(slot);
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
    const auto policy = impl_->interactions[size_t(id)];
    return Result<AgentInteraction>::success(
        {policy.pushability, policy.holdPosition, policy.contactLayer, policy.contactMask});
}

Result<InteractionPolicy> Crowd::readInteractionPolicy(AgentRef reference) const {
    const int slot = impl_->resolveAgent(reference);
    if (slot < 0)
        return Result<InteractionPolicy>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Crowd agent not found", "reference"));
    return Result<InteractionPolicy>::success(impl_->interactions[std::size_t(slot)]);
}

}  // namespace eve::crowd
