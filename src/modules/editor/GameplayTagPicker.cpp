#include "editor/GameplayTagPicker.h"

#include <algorithm>

namespace eve::editor {

Result<void> GameplayTagPicker::setRoot(std::string root) {
    if (!root.empty() && !tags::isValidGameplayTagName(root))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid gameplay-tag picker root", "root"));
    root_ = std::move(root);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> GameplayTagPicker::select(std::string name) {
    if (!registry_.contains(name))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Cannot select an unregistered gameplay tag", name));
    selected_ = std::move(name);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::vector<GameplayTagPickerEntry> GameplayTagPicker::entries() const {
    const auto                          definitions = registry_.search(filter_, root_);
    std::vector<GameplayTagPickerEntry> result;
    result.reserve(definitions.size());
    for (const auto& definition : definitions) {
        const std::size_t depth =
            static_cast<std::size_t>(std::count(definition.name.begin(), definition.name.end(), '.'));
        const bool hasChildren = std::any_of(definitions.begin(), definitions.end(), [&](const auto& candidate) {
            return candidate.name != definition.name &&
                   tags::gameplayTagMatches(candidate.name, definition.name,
                                            tags::GameplayTagMatch::IncludeDescendants);
        });
        result.push_back({definition.id, definition.name, definition.description, depth, hasChildren});
    }
    return result;
}

}  // namespace eve::editor
