#include "combat/ComboGraph.h"

#include <algorithm>
#include <utility>

namespace eve::combat {
  // namespace

Result<void> ComboGraphEdge::validate() const {
    if (from.format().empty() || to.format().empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "combo edge ids are empty", "id"));
    if (requiredInput.empty()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "combo edge input is empty", "requiredInput"));
    return Result<void>::success();
}

Result<void> ComboGraph::addEdge(ComboGraphEdge edge) {
    auto valid = edge.validate();
    if (!valid) return valid;
    for (const auto& existing : edges_) {
        if (existing.from == edge.from && existing.to == edge.to && existing.requiredInput == edge.requiredInput)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "combo edge already exists", "edge"));
    }
    edges_.push_back(std::move(edge));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<ComboGraphMatch> ComboGraph::match(const LogicalId& from, std::string_view input, bool allowCancel,
                                          bool allowCombo) const {
    const ComboGraphEdge* best = nullptr;
    for (const auto& edge : edges_) {
        if (edge.from != from) continue;
        if (edge.requiredInput != input) continue;
        if (edge.requiresCancelWindow && !allowCancel) continue;
        if (edge.requiresComboWindow && !allowCombo) continue;
        if (!best || edge.priority > best->priority ||
            (edge.priority == best->priority && edge.to.format() < best->to.format()))
            best = &edge;
    }
    if (!best)
        return Result<ComboGraphMatch>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no combo transition matched", "input"));
    return Result<ComboGraphMatch>::success(
        ComboGraphMatch{best->from, best->to, best->requiredInput, best->priority});
}

}  // namespace eve::combat
