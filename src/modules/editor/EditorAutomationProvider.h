#pragma once

#include "common/EditorAutomation.h"
#include "editor/EditorResult.h"
#include "editor/EditorSession.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::material_editing {
class IMaterialRuntimeSink;
}

namespace eve::editor {

class EditorTargetCoordinator;
class EditorCommandService;

/** @brief MCP/DevTools adapter over command discovery and target coordination. */
class EditorAutomationProvider final : public eve::IEditorAutomation {
public:
    /** @brief Bind borrowed services that outlive this provider. */
    EditorAutomationProvider(EditorCommandService& commands, EditorTargetCoordinator& targets);
    ~EditorAutomationProvider() override;

    /** @brief Invoke discovery, inspect, execute, plan, commit, undo or redo. */
    std::string invoke(const std::string& operation, const std::string& requestJson) override;

    /** @brief Clear automation state that still borrows an unregistered target. */
    void targetUnregistered(const TargetId& target);

private:
    struct ObservationSession;

    void refreshProfile();
    std::string commandsJson();
    EditorResult<void> bindRequestedTarget(const EditorValue::Object& request);
    std::string createTarget(const EditorValue::Object& request);
    std::string closeTarget(const EditorValue::Object& request);
    std::string startObservation(const EditorValue::Object& request);
    std::string describeObservation(const EditorValue::Object& request) const;
    std::string publishObservation(const EditorValue::Object& request);
    std::string pollObservation(const EditorValue::Object& request);
    std::string closeObservation(const EditorValue::Object& request);

    EditorCommandService*         commands_ = nullptr;
    EditorTargetCoordinator*      targets_ = nullptr;
    EditorSession                 session_;
    std::vector<EditorDiagnostic> lastDiagnostics_;
    // Targets borrow their runtime sinks, so sinks must be destroyed after targets.
    std::map<TargetId, std::unique_ptr<material_editing::IMaterialRuntimeSink>> ownedMaterialSinks_;
    std::map<TargetId, std::unique_ptr<IEditableTarget>>      ownedTargets_;
    std::map<std::string, std::unique_ptr<ObservationSession>> observationSessions_;
    std::uint64_t                                              nextObservationSession_ = 1;
};

}  // namespace eve::editor
