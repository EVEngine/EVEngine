#pragma once
#include "common/Export.h"

#include "editing/EditorPackage.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorMcpProjector.h"
#include "editor/EditorResult.h"
#include "editor/EditorSelectionRouter.h"
#include "editor/EditorUndoCoordinator.h"
#include "editor/EditorValue.h"
#include "editor/EditorWorkspace.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::editor {

class Editor;
class EditorCommandService;

/** @brief Per-owner mount overrides. */
struct EditorHostMountOverride {
    editing::EditorUndoPolicy undoPolicy      = editing::EditorUndoPolicy::Share;
    bool                      allowIncomplete = false;
};

/** @brief Summary of one successful or skipped package mount. */
struct EditorHostMountEntry {
    std::string ownerModule;
    std::size_t panelCount   = 0;
    std::size_t commandCount = 0;
    std::size_t targetCount  = 0;
    bool        skipped      = false;
    std::string skipReason;
};

/** @brief Aggregate mount report returned by autoMount/mount. */
struct EditorHostMountReport {
    std::vector<EditorHostMountEntry> entries;
    std::size_t                       mountedPackages = 0;
};

/**
 * @brief Composition runtime that discovers EditorPackage providers and installs them.
 *
 * Owns a workspace and tracks mounted owners for unmount cleanup. Does not own
 * domain document contents.
 */
class EVENGINE_API_ORCHESTRATION EditorHost {
public:
    struct Options {
        std::string id    = "editor.host";
        std::string title = "Editor Host";
        HostProfile profile;
        std::string selectionChannel = "default";
    };

    /** @brief Create a host bound to an Editor module's command service. */
    explicit EditorHost(Editor& editor, Options options);

    /** @brief Destroy host, detach MCP projection if attached, and unmount packages. */
    ~EditorHost();

    EditorHost(const EditorHost&)            = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    /** @brief Discover every IEditorPackageProvider and mount complete packages. */
    [[nodiscard]] Result<EditorHostMountReport> autoMount();

    /**
     * @brief Mount an explicit owner allowlist.
     * @param owners Module owner names (e.g. "action_editor"). Empty means all discovered.
     */
    [[nodiscard]] Result<EditorHostMountReport> mount(
        const std::vector<std::string>&                                 owners    = {},
        const std::unordered_map<std::string, EditorHostMountOverride>& overrides = {});

    /** @brief Unmount one owner and remove its workspace panels. */
    [[nodiscard]] Result<void> unmount(std::string_view ownerModule);

    /** @brief Unmount every mounted package. */
    void unmountAll();

    /** @brief Workspace receiving merged panels. */
    [[nodiscard]] EditorWorkspace& workspace() { return workspace_; }
    /** @brief Workspace receiving merged panels. */
    [[nodiscard]] const EditorWorkspace& workspace() const { return workspace_; }

    /** @brief Host selection channel used by SelectionRouter. */
    [[nodiscard]] const std::string& selectionChannel() const { return options_.selectionChannel; }

    /** @brief Default selection → inspector router. */
    [[nodiscard]] EditorSelectionRouter& selection() { return *selection_; }
    /** @brief Default selection → inspector router. */
    [[nodiscard]] const EditorSelectionRouter& selection() const { return *selection_; }

    /** @brief Focus-aware undo coordinator. */
    [[nodiscard]] EditorUndoCoordinator& undo() { return *undo_; }
    /** @brief Focus-aware undo coordinator. */
    [[nodiscard]] const EditorUndoCoordinator& undo() const { return *undo_; }

    /** @brief Attach this host as the active Mode A MCP projection scope. */
    [[nodiscard]] Result<void> exportMcp(EditorMcpExportMode mode = EditorMcpExportMode::Commands);

    /** @brief Export mounted command catalog (id + inputSchema) for tests/MCP. */
    [[nodiscard]] Result<EditorValue> exportCommandCatalog() const;

    /** @brief Export discovered target types with createSchema when known. */
    [[nodiscard]] Result<EditorValue> exportTargetCatalog() const;

    /** @brief Completeness audit (incomplete packages + orphan factory types). */
    [[nodiscard]] Result<EditorValue> auditPackages() const;

    /** @brief True when owner is currently mounted. */
    [[nodiscard]] bool isMounted(std::string_view ownerModule) const;

    /**
     * @brief Mounted package owner names.
     * @cost Linear in mounted package count; returns an owning vector copy.
     */
    [[nodiscard]] std::vector<std::string> mountedOwnerNames() const;

    /**
     * @brief Target types contributed by mounted packages.
     * @cost Linear in mounted target types; returns a sorted owning vector copy.
     */
    [[nodiscard]] std::vector<std::string> mountedTargetTypeNames() const;

    /** @brief Undo policy recorded for a mounted owner (Share when unknown). */
    [[nodiscard]] editing::EditorUndoPolicy mountedUndoPolicy(std::string_view ownerModule) const;

private:
    struct MountedPackage {
        editing::EditorPackage   package;
        std::vector<std::string> panelIds;
    };

    [[nodiscard]] Result<EditorHostMountEntry> mountPackage(const editing::EditorPackage&  package,
                                                            const EditorHostMountOverride& override);
    [[nodiscard]] Result<void> installPanels(const editing::EditorPackage& package, std::vector<std::string>& panelIds);

    Editor*                                         editor_   = nullptr;
    EditorCommandService*                           commands_ = nullptr;
    Options                                         options_;
    EditorWorkspace                                 workspace_;
    std::unique_ptr<EditorSelectionRouter>          selection_;
    std::unique_ptr<EditorUndoCoordinator>          undo_;
    std::unordered_map<std::string, MountedPackage> mounted_;
    bool                                            hasPrimaryViewport_ = false;
};

}  // namespace eve::editor
