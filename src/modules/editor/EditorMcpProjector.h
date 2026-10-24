#pragma once
#include "common/Export.h"

#include "editor/EditorResult.h"
#include "editor/EditorValue.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace eve::editor {

class EditorHost;

/** @brief MCP export mode for an attached EditorHost. */
enum class EditorMcpExportMode {
    Commands,  ///< Mode A: keep eve_editor_* with schema enrichment (default).
};

/**
 * @brief Host-scoped MCP projection over IEditorAutomation discovery.
 *
 * When attached, commands/target-list are filtered to mounted packages. Mode A
 * only — per-command dynamic tools remain out of scope.
 */
class EVENGINE_API_ORCHESTRATION EditorMcpProjector {
public:
    EditorMcpProjector() = default;

    /**
     * @brief Attach a borrowed host as the active MCP projection scope.
     * @param host Host that outlives the attachment; null detaches.
     * @param mode Export mode (default Commands / Mode A).
     */
    [[nodiscard]] Result<void> attach(EditorHost* host, EditorMcpExportMode mode = EditorMcpExportMode::Commands);

    /** @brief Clear the active attachment. */
    void detach();

    /** @brief Borrowed attached host, or null. */
    [[nodiscard]] EditorHost* host() const { return host_; }

    /** @brief Active export mode. */
    [[nodiscard]] EditorMcpExportMode mode() const { return mode_; }

    /** @brief True when a host is attached. */
    [[nodiscard]] bool isAttached() const { return host_ != nullptr; }

    /**
     * @brief Mounted owner allowlist, empty when detached (no filter).
     * @cost Linear in mounted package count; returns an owning set copy.
     */
    [[nodiscard]] std::unordered_set<std::string> mountedOwners() const;

    /**
     * @brief Mounted target type allowlist, empty when detached (no filter).
     * @cost Linear in mounted target types; returns an owning set copy.
     */
    [[nodiscard]] std::unordered_set<std::string> mountedTargetTypes() const;

    /**
     * @brief Audit incomplete packages and orphan automation factories.
     * @return Applied object with incompletePackages[] and orphanFactoryTypes[].
     */
    [[nodiscard]] Result<EditorValue> auditCompleteness() const;

private:
    EditorHost*         host_ = nullptr;
    EditorMcpExportMode mode_ = EditorMcpExportMode::Commands;
};

/** @brief Process-wide projector used by EditorAutomationProvider and eve.mcp.attachEditor. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION EditorMcpProjector& editorMcpProjector();

}  // namespace eve::editor
