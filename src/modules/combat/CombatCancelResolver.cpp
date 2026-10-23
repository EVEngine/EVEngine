#include "combat/CombatCancelResolver.h"

namespace eve::combat {

Result<CombatCancelResolution> CombatCancelResolver::resolve(const CombatCancelResolveRequest& request) {
    if (!buffer_ || !cancels_)
        return Result<CombatCancelResolution>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "cancel resolver requires buffer and cancel windows", "resolver"));
    if (!request.subject.isValid())
        return Result<CombatCancelResolution>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "cancel subject is nil", "subject"));
    if (request.currentAbility.format().empty())
        return Result<CombatCancelResolution>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "current ability id is empty", "currentAbility"));

    auto expired = buffer_->expire(request.tick);
    if (!expired) return Result<CombatCancelResolution>::failure(expired.status());

    const auto allows = cancels_->availableCancels(request.subject);
    if (allows.empty())
        return Result<CombatCancelResolution>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no active cancel windows", "cancels"));

    auto consumed = buffer_->consume(request.subject, allows);
    if (!consumed) return Result<CombatCancelResolution>::failure(consumed.status());

    auto cancel = cancels_->match(request.subject, consumed.value().input);
    if (!cancel) return Result<CombatCancelResolution>::failure(cancel.status());

    CombatCancelResolution resolution;
    resolution.consumed = std::move(consumed).takeValue();
    resolution.cancel   = cancel.value();

    bool allowCombo = false;
    if (combos_) {
        auto combo = combos_->match(request.subject, resolution.consumed.input);
        if (combo) {
            resolution.comboWindow = combo.value();
            allowCombo             = true;
        }
    }

    if (graph_) {
        auto matched = graph_->match(request.currentAbility, resolution.consumed.input, true, allowCombo);
        if (matched)
            resolution.graph = matched.value();
        else if (matched.status().code() != StatusCode::NotFound)
            return Result<CombatCancelResolution>::failure(matched.status());
    }
    return Result<CombatCancelResolution>::success(std::move(resolution), Status::success(StatusCode::Applied));
}

}  // namespace eve::combat
