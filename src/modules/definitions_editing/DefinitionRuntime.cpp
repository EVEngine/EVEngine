#include "definitions_editing/DefinitionTarget.h"

#include "definitions/Definitions.h"

#include <utility>

namespace eve::definitions_editing {

EditorResult<void> DefinitionRuntimePublisher::publish(const DefinitionDocument& document,
                                                       definitions::DefinitionRegistry* registry,
                                                       bool replaceExisting) const {
    if (!registry)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.definition.registry-required"),
                                         "Definition registry is required");
    const auto result = replaceExisting
                            ? registry->replace(document.type(), document.id(), document.version(), document.json())
                            : registry->insert(document.type(), document.id(), document.version(), document.json());
    if (!result.ok()) {
        const auto* diagnostic = result.error();
        return EditorResult<void>::error(
            EditorStatus::Rejected, RuleId("editor.definition.publish-rejected"),
            diagnostic ? diagnostic->message() : "Definition registry rejected the document");
    }
    return EditorResult<void>::applied();
}

}  // namespace eve::definitions_editing
