#include "editor/EditCommand.h"
#include <algorithm>
#include <utility>
#include "common/Exception.h"
#include "editor/EditConstraint.h"

// EditCommand.cpp
namespace eve::editor {

IntFieldEditCommand::IntFieldEditCommand(std::string name, IEditableTarget *target)
    : name_(std::move(name)), target_(target) {}
bool IntFieldEditCommand::record(int x, int y, int after) {
    auto *field = target_ ? target_->query<IIntFieldTarget>() : nullptr;
    if (!field || !field->containsCell(x, y)) return false;
    for (auto &change : changes_) {
        if (change.x == x && change.y == y) {
            change.after = after;
            return true;
        }
    }
    const int before = field->readInt(x, y);
    if (before == after) return false;
    changes_.push_back({x, y, before, after});
    dirty_.include(x, y);
    return true;
}
bool IntFieldEditCommand::apply() {
    auto *field = target_ ? target_->query<IIntFieldTarget>() : nullptr;
    if (!field || changes_.empty()) return false;
    for (std::size_t i = 0; i < changes_.size(); ++i) {
        if (field->writeInt(changes_[i].x, changes_[i].y, changes_[i].after) == FieldWriteStatus::Rejected) {
            while (i > 0) {
                --i;
                (void)field->writeInt(changes_[i].x, changes_[i].y, changes_[i].before);
            }
            return false;
        }
    }
    return true;
}
void IntFieldEditCommand::revert() {
    auto *field = target_ ? target_->query<IIntFieldTarget>() : nullptr;
    if (!field) return;
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) (void)field->writeInt(it->x, it->y, it->before);
}
std::unique_ptr<IEditCommand> IntFieldEditCommand::clone() const {
    return std::make_unique<IntFieldEditCommand>(*this);
}
bool IntFieldEditCommand::mergeWith(const IEditCommand &laterBase) {
    auto *later = dynamic_cast<const IntFieldEditCommand *>(&laterBase);
    if (!later || later->target_ != target_) return false;
    for (const auto &incoming : later->changes_) {
        bool merged = false;
        for (auto &current : changes_) {
            if (current.x == incoming.x && current.y == incoming.y) {
                current.after = incoming.after;
                merged        = true;
                break;
            }
        }
        if (!merged) changes_.push_back(incoming);
    }
    dirty_.include(later->dirty_);
    return true;
}

ScalarFieldEditCommand::ScalarFieldEditCommand(std::string name, IEditableTarget *target)
    : name_(std::move(name)), target_(target) {}
bool ScalarFieldEditCommand::record(int x, int y, float after) {
    auto *field = target_ ? target_->query<IScalarFieldTarget>() : nullptr;
    if (!field || !field->containsCell(x, y)) return false;
    for (auto &change : changes_) {
        if (change.x == x && change.y == y) {
            change.after = after;
            return true;
        }
    }
    const float before = field->readScalar(x, y);
    if (before == after) return false;
    changes_.push_back({x, y, before, after});
    dirty_.include(x, y);
    return true;
}
bool ScalarFieldEditCommand::apply() {
    auto *field = target_ ? target_->query<IScalarFieldTarget>() : nullptr;
    if (!field || changes_.empty()) return false;
    for (std::size_t i = 0; i < changes_.size(); ++i) {
        if (field->writeScalar(changes_[i].x, changes_[i].y, changes_[i].after) == FieldWriteStatus::Rejected) {
            while (i > 0) {
                --i;
                (void)field->writeScalar(changes_[i].x, changes_[i].y, changes_[i].before);
            }
            return false;
        }
    }
    return true;
}
void ScalarFieldEditCommand::revert() {
    auto *field = target_ ? target_->query<IScalarFieldTarget>() : nullptr;
    if (!field) return;
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it)
        static_cast<void>(field->writeScalar(it->x, it->y, it->before));
}
std::unique_ptr<IEditCommand> ScalarFieldEditCommand::clone() const {
    return std::make_unique<ScalarFieldEditCommand>(*this);
}
bool ScalarFieldEditCommand::mergeWith(const IEditCommand &laterBase) {
    auto *later = dynamic_cast<const ScalarFieldEditCommand *>(&laterBase);
    if (!later || later->target_ != target_) return false;
    for (const auto &incoming : later->changes_) {
        bool merged = false;
        for (auto &current : changes_) {
            if (current.x == incoming.x && current.y == incoming.y) {
                current.after = incoming.after;
                merged        = true;
                break;
            }
        }
        if (!merged) changes_.push_back(incoming);
    }
    dirty_.include(later->dirty_);
    return true;
}

}  // namespace eve::editor

// EditConstraint.cpp
namespace eve::editor {

ConstraintResult ConstraintResult::warning(std::string message) {
    return {ConstraintDisposition::Warning, std::move(message)};
}
ConstraintResult ConstraintResult::reject(std::string message) {
    return {ConstraintDisposition::Reject, std::move(message)};
}
bool EditConstraintPipeline::add(IEditConstraint *constraint) {
    if (!constraint || std::find(constraints_.begin(), constraints_.end(), constraint) != constraints_.end())
        return false;
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
Result<void> EditConstraintPipeline::evaluateChecked(EditorContext &context, IEditCommand &command) {
    diagnostics_.clear();
    structuredDiagnostics_.clear();
    rejected_   = false;
    auto record = [&](DiagnosticSeverity severity, const char *rule, std::string message) {
        if (message.empty() && severity != DiagnosticSeverity::Error) return;
        if (message.empty()) message = "Editor command was rejected by a constraint";
        structuredDiagnostics_.push_back(editing::ruleDiagnostic(severity == DiagnosticSeverity::Error
                                                                     ? eve::DiagnosticCode::PreconditionViolation
                                                                     : eve::DiagnosticCode::None,
                                                                 RuleId(rule), severity, message));
        diagnostics_.push_back(std::move(message));
    };
    for (auto *constraint : constraints_) {
        ConstraintResult result = constraint->evaluate(context, command);
        if (result.disposition == ConstraintDisposition::Reject) {
            rejected_ = true;
            record(DiagnosticSeverity::Error, "editor.command.constraint-rejected", std::move(result.message));
            return Result<void>::failure(eve::Status(EditorStatus::Rejected, structuredDiagnostics_));
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
