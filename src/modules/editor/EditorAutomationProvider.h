#pragma once

#include "common/EditorAutomation.h"
#include "editor/EditorResult.h"
#include "editor/EditorSession.h"

#include <string>
#include <vector>

namespace eve::editor {

class EditorTargetCoordinator;
class EditorCommandService;

/** @brief MCP/DevTools adapter over command discovery and target coordination. */
class EditorAutomationProvider final : public eve::IEditorAutomation {
public:
    /** @brief Bind borrowed services that outlive this provider. */
    EditorAutomationProvider(EditorCommandService& commands, EditorTargetCoordinator& targets);

    /** @brief Invoke discovery, inspect, execute, plan, commit, undo or redo. */
    std::string invoke(const std::string& operation, const std::string& requestJson) override;

    /** @brief Clear automation state that still borrows an unregistered target. */
    void targetUnregistered(const TargetId& target);

private:
    void refreshProfile();
    std::string commandsJson();
    EditorResult<void> bindRequestedTarget(const EditorValue::Object& request);

    EditorCommandService*         commands_ = nullptr;
    EditorTargetCoordinator*      targets_ = nullptr;
    EditorSession                 session_;
    std::vector<EditorDiagnostic> lastDiagnostics_;
};

}  // namespace eve::editor
