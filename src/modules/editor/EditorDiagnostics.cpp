#include "editor/EditorDiagnostics.h"

namespace eve::editor {

EditorResult<void> EditorValidationService::registerRule(std::string owner, RuleId id, Rule rule) {
    if (owner.empty() || id.empty() || !rule)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.validation.invalid-rule"),
                                         "Validation owner, rule id and callback are required");
    rules_.insert_or_assign(std::move(id), RegisteredRule{std::move(owner), std::move(rule)});
    return eve::editing::applied<void>();
}

std::size_t EditorValidationService::unregisterOwner(const std::string& owner) {
    return std::erase_if(rules_, [&](const auto& entry) { return entry.second.owner == owner; });
}

std::vector<EditorDiagnostic> EditorValidationService::validate(const ValidationRequest& request) const {
    std::vector<EditorDiagnostic> diagnostics;
    for (const auto& [id, registered] : rules_) {
        (void)id;
        auto current = registered.rule(request);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(current.begin()),
                           std::make_move_iterator(current.end()));
    }
    return diagnostics;
}

void EditorDiagnosticService::publish(std::string source, std::vector<EditorDiagnostic> diagnostics) {
    std::lock_guard lock(mutex_);
    channels_.insert_or_assign(std::move(source), std::move(diagnostics));
}

void EditorDiagnosticService::clear(const std::string& source) {
    std::lock_guard lock(mutex_);
    channels_.erase(source);
}

std::vector<EditorDiagnostic> EditorDiagnosticService::snapshot() const {
    std::lock_guard               lock(mutex_);
    std::vector<EditorDiagnostic> result;
    for (const auto& [source, diagnostics] : channels_) {
        (void)source;
        result.insert(result.end(), diagnostics.begin(), diagnostics.end());
    }
    return result;
}

}  // namespace eve::editor
