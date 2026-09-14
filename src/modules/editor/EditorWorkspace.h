#pragma once

#include "editor/EditorDock.h"
#include "editor/EditorSelection.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace eve::editor {

/** @brief Supported semantic dock regions for UI-neutral workspace panels. */
enum class WorkspaceRegion { Left, Right, Top, Bottom, Center, Floating };

/** @brief Return the stable serialized name for a workspace region. */
std::string_view workspaceRegionName(WorkspaceRegion region);

/** @brief UI-neutral description of one composable editor panel. */
struct WorkspacePanelDescriptor {
    std::string id;
    std::string title;
    std::string region = "center";
    std::string capability;
    std::string context;
    int         order     = 0;
    bool        visible   = true;
    bool        singleton = true;
};

/**
 * @brief Composition model shared by project-specific developer and runtime editors.
 *
 * The workspace deliberately contains no rendering or widget code. Games register
 * semantic panels and render the descriptors with `ui`, an external toolkit, or an
 * automation host. Selection and focus use the same channelled V2 services used by
 * command/property providers, so a custom editor and an in-game builder can share
 * their model without sharing a pixel layout.
 */
class EditorWorkspace {
public:
    /** @brief Create a workspace with stable identity and display title. */
    EditorWorkspace(std::string id, std::string title);

    /** @brief Stable project-defined workspace id. */
    const std::string& getId() const { return id_; }
    /** @brief User-facing workspace title. */
    const std::string& getTitle() const { return title_; }
    /** @brief Change the display title without changing workspace identity. */
    void setTitle(const std::string& title);

    /** @brief Register a checked owning panel descriptor. */
    [[nodiscard]] EditorResult<WorkspacePanelDescriptor> registerPanel(WorkspacePanelDescriptor descriptor);
    /** @brief Move a panel using a strongly typed dock region. */
    [[nodiscard]] EditorResult<WorkspacePanelDescriptor> movePanel(const StableId& id, WorkspaceRegion region,
                                                                  int order);
    /** @brief Remove a panel and return its final owning descriptor. */
    [[nodiscard]] EditorResult<WorkspacePanelDescriptor> removePanel(const StableId& id);
    /** @brief Activate a visible panel with structured not-found/rejected results. */
    [[nodiscard]] EditorResult<WorkspacePanelDescriptor> activatePanel(const StableId& id);
    /** @brief Return an owning panel snapshot instead of an ambiguous empty field. */
    [[nodiscard]] EditorResult<WorkspacePanelDescriptor> panelAt(std::size_t index) const;

    /**
     * @brief Register a panel descriptor.
     * @return False for an empty/duplicate id or an unsupported dock region.
     * @note Compatibility-only string/boolean facade over registerPanel(descriptor).
     */
    bool registerPanel(const std::string& id, const std::string& title, const std::string& region, int order);
    /** @brief Remove a panel descriptor by id. */
    bool removePanel(const std::string& id);
    /** @brief Remove every panel descriptor. */
    void clearPanels();
    /** @brief Compatibility-only string/boolean facade over movePanel(id, region, order). */
    bool movePanel(const std::string& id, const std::string& region, int order);
    /** @brief Attach a capability requirement used by the host when filtering panels. */
    bool setPanelCapability(const std::string& id, const std::string& capability);
    /** @brief Attach a semantic input context such as scene, graph, or timeline. */
    bool setPanelContext(const std::string& id, const std::string& context);
    /** @brief Show or hide a panel without removing its descriptor. */
    bool setPanelVisible(const std::string& id, bool visible);
    /** @brief Mark whether hosts should allow more than one instance of this panel type. */
    bool setPanelSingleton(const std::string& id, bool singleton);
    /** @brief Make a registered visible panel the active semantic surface. */
    bool activatePanel(const std::string& id);
    /** @brief Active panel id, or an empty string. */
    const std::string& getActivePanel() const { return activePanel_; }

    /** @brief Number of registered panels in deterministic region/order/id order. */
    int getPanelCount() const { return static_cast<int>(panels_.size()); }
    /** @brief Panel id by deterministic index. */
    std::string getPanelId(int index) const;
    /** @brief Panel title by deterministic index. */
    std::string getPanelTitle(int index) const;
    /** @brief Panel dock region by deterministic index. */
    std::string getPanelRegion(int index) const;
    /** @brief Panel capability requirement by deterministic index. */
    std::string getPanelCapability(int index) const;
    /** @brief Panel input context by deterministic index. */
    std::string getPanelContext(int index) const;
    /** @brief Panel order by deterministic index. */
    int getPanelOrder(int index) const;
    /** @brief Panel visibility by deterministic index. */
    bool getPanelVisible(int index) const;
    /** @brief Panel singleton policy by deterministic index. */
    bool getPanelSingleton(int index) const;

    /** @brief Configure one standard dock region size in pixels. */
    void setRegionSize(const std::string& region, float pixels);
    /** @brief Compute standard dock rectangles for the requested host size. */
    void layout(float width, float height);
    /** @brief Computed region X coordinate. */
    float getRegionX(const std::string& region) const;
    /** @brief Computed region Y coordinate. */
    float getRegionY(const std::string& region) const;
    /** @brief Computed region width. */
    float getRegionW(const std::string& region) const;
    /** @brief Computed region height. */
    float getRegionH(const std::string& region) const;

    /** @brief Set a project-defined mode such as edit, play, material, or animation. */
    bool setMode(const std::string& mode);
    /** @brief Set a strongly identified workspace mode. */
    [[nodiscard]] EditorResult<StableId> setModeId(StableId mode);
    /** @brief Current project-defined mode. */
    const std::string& getMode() const { return mode_; }

    /**
     * @brief Select one stable item in a semantic channel.
     * @param additive Preserve existing items in the channel when true.
     */
    bool select(const std::string& channel, const std::string& domain, const std::string& target,
                const std::string& item, const std::string& type, bool additive);
    /** @brief Select one already typed item without string domain parsing. */
    [[nodiscard]] EditorResult<SelectionSnapshot> selectItem(std::string channel, SelectionItem item, bool additive);
    /** @brief Clear one semantic selection channel. */
    bool clearSelection(const std::string& channel);
    /** @brief Clear one selection channel without discarding diagnostics. */
    [[nodiscard]] EditorResult<SelectionSnapshot> clearSelectionChecked(const std::string& channel);
    /** @brief Number of selected items in a channel. */
    int getSelectionCount(const std::string& channel) const;
    /** @brief Selected stable item id by channel/index. */
    std::string getSelectionItem(const std::string& channel, int index) const;
    /** @brief Selected item type by channel/index. */
    std::string getSelectionType(const std::string& channel, int index) const;
    /** @brief Primary selected item id for a channel. */
    std::string getPrimarySelection(const std::string& channel) const;
    /** @brief Monotonic selection sequence for MVVM change detection. */
    std::uint64_t getSelectionSequence(const std::string& channel) const;
    /** @brief Set UI-toolkit-independent focus for a channel. */
    bool focus(const std::string& channel, const std::string& surface, const std::string& item);
    /** @brief Focus a strongly identified surface and item without discarding diagnostics. */
    [[nodiscard]] EditorResult<EditorFocusSnapshot> focusItem(const std::string& channel, StableId surface,
                                                              StableId item);
    /** @brief Focused surface id for a channel. */
    std::string getFocusedSurface(const std::string& channel) const;

    /** @brief Monotonic structural revision for cheap ViewModel synchronization. */
    std::uint64_t getRevision() const { return revision_; }

private:
    static bool                     isRegion(const std::string& region);
    static bool                     parseDomain(const std::string& value, SelectionDomain& domain);
    /** @return Borrowed panel storage, or null. @lifetime Valid until the next workspace mutation. */
    WorkspacePanelDescriptor* findPanel(const std::string& id);
    /** @return Borrowed panel storage, or null. @lifetime Valid until the next workspace mutation. */
    const WorkspacePanelDescriptor* panelAtUnchecked(int index) const;
    void                            sortPanels();
    void                            changed();

    std::string                           id_;
    std::string                           title_;
    std::string                           mode_ = "edit";
    std::string                           activePanel_;
    std::vector<WorkspacePanelDescriptor> panels_;
    EditorDock                            dock_;
    EditorSelectionService                selection_;
    EditorFocusService                    focus_;
    std::uint64_t                         revision_ = 0;
};

}  // namespace eve::editor
