#include "editor/EditConstraint.h"

#include "common/Exception.h"

#include <algorithm>
#include <utility>

namespace eve::editor {

ConstraintResult ConstraintResult::warning(std::string message) {
    return {ConstraintDisposition::Warning, std::move(message)};
}
ConstraintResult ConstraintResult::reject(std::string message) {
    return {ConstraintDisposition::Reject, std::move(message)};
}
bool EditConstraintPipeline::add(IEditConstraint *constraint) {
    if (!constraint || std::find(constraints_.begin(), constraints_.end(), constraint) != constraints_.end()) return false;
    constraints_.push_back(constraint);
    return true;
}
bool EditConstraintPipeline::remove(IEditConstraint *constraint) {
    auto it = std::find(constraints_.begin(), constraints_.end(), constraint);
    if (it == constraints_.end()) return false;
    constraints_.erase(it);
    return true;
}
void EditConstraintPipeline::clear() {
    constraints_.clear();
    diagnostics_.clear();
    structuredDiagnostics_.clear();
    rejected_ = false;
}
bool EditConstraintPipeline::evaluate(EditorContext &context, IEditCommand &command) {
    return evaluateChecked(context, command).ok();
}
EditorResult<void> EditConstraintPipeline::evaluateChecked(EditorContext &context, IEditCommand &command) {
    diagnostics_.clear();
    structuredDiagnostics_.clear();
    rejected_ = false;
    auto record = [&](DiagnosticSeverity severity, const char *rule, std::string message) {
        if (message.empty() && severity != DiagnosticSeverity::Error) return;
        if (message.empty()) message = "Editor command was rejected by a constraint";
        structuredDiagnostics_.push_back(editing::ruleDiagnostic(
            severity == DiagnosticSeverity::Error ? eve::DiagnosticCode::PreconditionViolation
                                                  : eve::DiagnosticCode::None,
            RuleId(rule), severity, message));
        diagnostics_.push_back(std::move(message));
    };
    for (auto *constraint : constraints_) {
        ConstraintResult result = constraint->evaluate(context, command);
        if (result.disposition == ConstraintDisposition::Reject) {
            rejected_ = true;
            record(DiagnosticSeverity::Error, "editor.command.constraint-rejected", std::move(result.message));
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, structuredDiagnostics_));
        }
        if (result.disposition == ConstraintDisposition::Warning)
            record(DiagnosticSeverity::Warning, "editor.command.constraint-warning", std::move(result.message));
        else if (!result.message.empty())
            record(DiagnosticSeverity::Info, "editor.command.constraint-info", std::move(result.message));
    }
    return eve::editing::applied<void>(structuredDiagnostics_);
}
const std::string &EditConstraintPipeline::diagnostic(int index) const {
    if (index < 0 || index >= diagnosticCount()) throw Exception("EditConstraintPipeline::diagnostic: bad index");
    return diagnostics_[static_cast<size_t>(index)];
}

}  // namespace eve::editor
