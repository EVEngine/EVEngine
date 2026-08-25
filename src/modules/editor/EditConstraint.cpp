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
    rejected_ = false;
}
bool EditConstraintPipeline::evaluate(EditorContext &context, IEditCommand &command) {
    diagnostics_.clear();
    rejected_ = false;
    for (auto *constraint : constraints_) {
        ConstraintResult result = constraint->evaluate(context, command);
        if (!result.message.empty()) diagnostics_.push_back(std::move(result.message));
        if (result.disposition == ConstraintDisposition::Reject) {
            rejected_ = true;
            return false;
        }
    }
    return true;
}
const std::string &EditConstraintPipeline::diagnostic(int index) const {
    if (index < 0 || index >= diagnosticCount()) throw Exception("EditConstraintPipeline::diagnostic: bad index");
    return diagnostics_[static_cast<size_t>(index)];
}

}  // namespace eve::editor
