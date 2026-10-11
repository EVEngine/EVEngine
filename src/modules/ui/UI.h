#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/Result.h"
#include "ui/NinePatch.h"
#include "ui/UIBackend.h"
#include "ui/UIHost.h"
#include "ui/UiTween.h"
#include "ui/Widget.h"

#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Canvas;
class Graphics;
class Texture;
}

namespace eve::ui {

class DatabasePanel;
class EditorShell;
class Inspector;
class ScenePanel;
struct UIEvent;

/**
 * @brief Declarative UI module (eve.UI).
 *
 * ECS: named UIHost panels. Builder + Component.build (C++/script) + list/when/theme.
 */
class EVENGINE_API_WORLD UI : public Module {
public:
    Module_REG(UI);
    /** @brief Ui. */
    UI();
    /** @brief Ui. */
    ~UI() override;

    /**
     * @brief Drops database-panel Squirrel roots while the Runtime VM is alive.
     * @remarks Invoked from Runtime shutdown via ISquirrelRootReleaser.
     */
    void releaseSquirrelRoots() noexcept;

    /** @brief Creates the platform UI backend (ImGui); true on success. */
    bool initBackend();
    /** @brief Destroys the platform UI backend. */
    void shutdownBackend();
    /** @brief True once the backend exists. */
    bool isBackendReady() const;

    /** @brief Feeds an SDL event into the UI backend (before window/game handling). */
    void processEvent(const SDL_Event *event);
    /** @brief Builds and presents the current frame's UI. */
    void beginFrameAndRender();
    /** @brief Dispatches queued widget callbacks (click/toggle/value/text). */
    void dispatchEvents();

    /** @brief True when the UI wants to capture mouse input this frame. */
    bool wantCaptureMouse() const;
    /** @brief True when the UI wants to capture keyboard input this frame. */
    bool wantCaptureKeyboard() const;

    /**
     * @brief Creates/replaces a named host from a WidgetDesc tree and selects it.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown; retain the generation handle across frames.
     * @thread Call on the UI thread.
     * @reentrancy The operation may rebuild widgets but does not invoke external callbacks before returning.
     */
    [[nodiscard]] UIHostHandle mountAs(const std::string &name, WidgetDesc root);
    /**
     * @brief Mounts the tree as an auto-named host and selects it.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown; retain the generation handle across frames.
     * @thread Call on the UI thread.
     * @reentrancy The operation may rebuild widgets but does not invoke external callbacks before returning.
     */
    [[nodiscard]] UIHostHandle mount(WidgetDesc root);
    /**
     * @brief Replaces the selected host's tree.
     * @return Borrowed nullable selected host, or null when no host is selected.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy Tree callbacks are not invoked while the returned pointer is being acquired.
     */
    [[nodiscard]] UIHostHandle remount(WidgetDesc root);
    /**
     * @brief Remount with key reconcile (props-only when structure matches).
     * @return Borrowed nullable selected host, or null when no host is selected.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy Reconciliation callbacks are completed before the pointer is returned.
     */
    [[nodiscard]] UIHostHandle remountReconcile(WidgetDesc root);
    /**
     * @brief Creates/replaces a named host (does not select it).
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy The operation does not invoke external callbacks before returning.
     */
    [[nodiscard]] UIHostHandle remountAs(const std::string &name, WidgetDesc root);

    /** @brief Selects a named host; false when it does not exist. */
    bool select(const std::string &name);
    /**
     * @brief Finds a host by name, or null when absent.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across host mutation.
     */
    [[nodiscard]] UIHostHandle findHost(const std::string &name) const;
    /**
     * @brief Finds the host bound to an owner id, or null when absent.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across host mutation.
     */
    [[nodiscard]] UIHostHandle findHostByOwner(uint32_t ownerId) const;
    /**
     * @brief Returns the currently selected host, or null.
     * @return Borrowed nullable host owned by the UI ECS world.
     * @ownership UI owns the host; callers must not delete it.
     * @lifetime Valid until selection/host removal or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across selection mutation.
     */
    /** @brief Returns the generation-qualified selected host handle, or empty. */
    [[nodiscard]] UIHostHandle current() const noexcept { return selected_; }
    /** @brief Binds the selected host to a UI/scene owner id. */
    void bindOwner(uint32_t ownerId);

    /** @brief Imperative builder: open a new build pass (see beginWindow etc.). */
    void beginBuild();
    /** @brief Opens a window for the current build pass. */
    void beginWindow(const std::string &title, const std::string &id = "root");
    /** @brief Opens a group container in the current build pass. */
    void beginGroup(const std::string &id = "");
    /** @brief Opens a list container (rows added with addListItem). */
    void beginList(const std::string &id);
    /** @brief Opens a collapsible header. */
    void beginCollapsing(const std::string &label, const std::string &id = "", bool open = true);
    /** @brief Opens a sized child region. */
    void beginChild(const std::string &id, float width = 0.f, float height = 120.f);
    /** @brief Opens a bordered card surface. */
    void beginCard(const std::string &id = "");
    /** @brief Opens a stretchable .9.png panel; false means the asset is invalid. */
    bool beginNinePatch(const std::string &path, const std::string &id = "",
                        float width = 0.f, float height = 0.f);
    /** @brief Opens a window menu bar. */
    void beginMenuBar(const std::string &id = "");
    /** @brief Opens a popup menu. */
    void beginMenu(const std::string &label, const std::string &id = "");
    /** @brief Opens a horizontal editor command strip. */
    void beginToolbar(const std::string &id = "");
    /** @brief Opens a wrapping editor tool grid. */
    void beginToolbox(const std::string &id = "", float cellSize = 0.f, int columns = 0);
    /** @brief Opens a vertical editor side panel. */
    void beginSidebar(const std::string &id = "", float width = 0.f);
    /** @brief Opens a compact horizontal status strip. */
    void beginStatusBar(const std::string &id = "");
    /** @brief Opens a two-child resizable split pane. */
    void beginSplitPane(const std::string &direction = "row", float ratio = 0.25f,
                        const std::string &id = "");
    /** Virtualized scroll list; rows are the children added before end(). */
    /** @brief Begins scroll list. */
    void beginScrollList(const std::string &id = "", float height = 0.f, float itemHeight = 0.f);
    /**
     * @brief Begin a Flex container.
     * @param direction "row" / "column" (case-insensitive; default row)
     * @param id stable node id
     * @param gap spacing between children; <0 uses theme ItemSpacing
     */
    void beginFlex(const std::string &direction = "row", const std::string &id = "",
                   float gap = -1.f);
    /** @brief Opens a row flex container. */
    void beginRow(const std::string &id = "", float gap = -1.f);
    /** @brief Opens a column flex container. */
    void beginColumn(const std::string &id = "", float gap = -1.f);
    /** @brief Begins a fixed-column grid container. */
    void beginGrid(int columns, const std::string &id = "", float columnGap = -1.f,
                   float rowGap = -1.f);
    /** @brief Closes the innermost open container. */
    void end();
    /** @brief Adds a text label to the current container. */
    void addText(const std::string &content, const std::string &id = "");
    /** Text with an explicit wrap width (0 = no wrap). */
    /** @brief Adds text wrapped. */
    void addTextWrapped(const std::string &content, float width, const std::string &id = "");
    /** @brief Adds a button to the current container. */
    void addButton(const std::string &label, const std::string &id = "");
    /** @brief Adds a semantic icon by name (for example "search" or "save"). */
    void addIcon(const std::string &name, const std::string &id = "");
    /** @brief Adds a semantic icon button with an optional visible label. */
    void addIconButton(const std::string &name, const std::string &label = "",
                       const std::string &id = "");
    /** @brief Adds an inline-break spacer. */
    void addSameLine(const std::string &id = "");
    /** @brief Adds a separator line. */
    void addSeparator(const std::string &id = "");
    /** @brief Adds a checkbox. */
    void addCheckbox(const std::string &label, bool checked, const std::string &id = "");
    /** @brief Adds a slider. */
    void addSlider(const std::string &label, float value, float minV, float maxV,
                   const std::string &id = "");
    /**
     * @brief Adds a color palette (ColorEdit + swatch grid).
     * RGBA is stored on the node tint; read back with getColorR/G/B/A.
     */
    void addColorPalette(const std::string &label, float r, float g, float b, float a = 1.f,
                         const std::string &id = "");
    /** @brief Adds a progress bar. */
    void addProgress(float fraction, const std::string &id = "", const std::string &overlay = "");
    /** Colored / textured image; size 0 = default (32px or flex-assigned). */
    /** @brief Adds image. */
    void addImage(const std::string &id = "", float width = 0.f, float height = 0.f);
    /** @brief Adds a .9.png image after removing its one-pixel marker frame. */
    bool addNinePatch(const std::string &path, const std::string &id = "",
                      float width = 0.f, float height = 0.f);
    /** Clickable image button (click routes through consumeClick / callbacks). */
    /** @brief Adds image button. */
    void addImageButton(const std::string &id, float width, float height);
    /** Embedded viewport widget (see viewportCanvas / viewport* input getters). */
    /** @brief Adds viewport. */
    void addViewport(const std::string &id, float width = 0.f, float height = 0.f);
    /** Dropdown; options separated by '\n', `selected` = initial index. */
    /** @brief Adds combo. */
    void addCombo(const std::string &label, const std::string &options, int selected,
                  const std::string &id = "");
    /** @brief Adds an editable text field. */
    void addInputText(const std::string &label, const std::string &value, const std::string &id = "");
    /** @brief Adds a compact search field. */
    void addSearchField(const std::string &hint, const std::string &value = "",
                        const std::string &id = "");
    /** @brief Adds a modern boolean switch. */
    void addSwitch(const std::string &label, bool checked, const std::string &id = "");
    /** @brief Adds a compact status/category badge. */
    void addBadge(const std::string &label, const std::string &id = "");
    /** @brief Adds a non-collapsible section heading. */
    void addSectionHeader(const std::string &label, const std::string &id = "");
    /** @brief Adds a selectable menu command. */
    void addMenuItem(const std::string &label, const std::string &shortcut = "",
                     const std::string &id = "");
    /** @brief Flexible empty space inside Flex (default grow=1). */
    void addSpacer(const std::string &id = "", float grow = 1.f);
    /**
     * @brief Set flex item props on the most recently added child of the current open container.
     * No-op if there is no current child.
     */
    void setItemFlexGrow(float grow);
    /** @brief Sets shrink weight on the most recently added flex item. */
    void setItemFlexShrink(float shrink);
    /** @brief Sets its main-axis basis; negative restores automatic measurement. */
    void setItemFlexBasis(float basis);
    /** @brief Sets its cross-axis alignment: inherit, start, center, end, or stretch. */
    void setItemAlignSelf(const std::string &align);
    /** @brief Sets width/height ratio on the most recently added item. */
    void setItemAspectRatio(float ratio);
    /** @brief Sets the grid-column span of the most recently added item. */
    void setItemGridColumnSpan(int span);
    /** @brief Sets width/height on the most recently added child. */
    void setItemSize(float width, float height);
    /** Layout box model on the most recently added child (no-op if none). */
    /** @brief Sets the item margin. */
    void setItemMargin(float l, float t, float r, float b);
    /** @brief Sets the item padding. */
    void setItemPadding(float l, float t, float r, float b);
    /** @brief Sets the item min size. */
    void setItemMinSize(float w, float h);
    /** @brief Sets the item max size. */
    void setItemMaxSize(float w, float h);
    /** @brief Sets the item percent. */
    void setItemPercent(float w, float h);
    /** Place the most recently added child absolutely inside the current Flex. */
    /** @brief Sets the item absolute. */
    void setItemAbsolute(float anchorX, float anchorY, float x = 0.f, float y = 0.f);
    /** @brief Sets hover help on the most recently added item. */
    void setItemTooltip(const std::string &text);
    /**
     * @brief Marks the most recently added item as a desktop drag source.
     * @param payloadType Stable application-defined type; "file" is reserved for OS files.
     * @param payloadText Owning UTF-8 payload copied into retained state.
     */
    void setItemDragSource(const std::string &payloadType, const std::string &payloadText);
    /**
     * @brief Marks the most recently added item as a desktop drop target.
     * @param acceptedType Exact payload type or "*" for any type.
     */
    void setItemDropTarget(const std::string &acceptedType);
    /** @brief Enables or disables the most recently added item. */
    void setItemEnabled(bool enabled);
    /**
     * @brief Marks the most recently added button as the selected tool/action.
     * @param selected Selected state.
     */
    void setItemSelected(bool selected);
    /** @brief Sets the last item's focus mode: "none", "click", or "all". */
    void setItemFocusMode(const std::string &mode);
    /** @brief Sets the last item's pointer filter: "stop", "pass", or "ignore". */
    void setItemMouseFilter(const std::string &filter);
    /** @brief Overrides the last item's subtree theme: "inherit", "dark", or "light". */
    void setItemTheme(const std::string &theme);
    /** @brief Overrides the current open container's inherited subtree theme. */
    void setThemeScope(const std::string &theme);
    /** @brief Applies a named style class to the most recently added item. */
    void setItemStyleClass(const std::string &name);
    /** @brief Applies a named style class to the current open container. */
    void setStyleScope(const std::string &name);
    /** @brief Sets the last item's sequential focus order; negative excludes it. */
    void setItemTabIndex(int index);
    /** @brief Sets explicit previous and next focus neighbors on the last item. */
    void setItemFocusOrder(const std::string &previous, const std::string &next);
    /** @brief Sets explicit directional focus neighbors on the last item. */
    void setItemFocusNeighbors(const std::string &left, const std::string &right,
                               const std::string &up, const std::string &down);
    /** @brief Sets the last item's accessibility role, name and description. */
    void setItemAccessibility(const std::string &role, const std::string &name,
                              const std::string &description = "");
    /** Set Flex container align/justify on the current open Flex (no-op otherwise). */
    /** @brief Set Flex container align/justify on the current open Flex (no-op otherwise). */
    void setFlexAlign(const std::string &align);
    /** @brief Sets Flex container justify on the current open Flex. */
    void setFlexJustify(const std::string &justify);
    /** @brief Sets independent column and row gaps on the current Flex/Grid. */
    void setLayoutGaps(float columnGap, float rowGap);
    /** @brief Enables or disables line wrapping on the current Flex. */
    void setFlexWrap(bool wrap);
    /** @brief Sets overflow behavior: visible, clip, or scroll. */
    void setLayoutOverflow(const std::string &overflow);
    /** @brief Append one list row button (call inside beginList). */
    void addListItem(const std::string &label, const std::string &id = "");

    /** @brief Finishes the build pass and mounts the built tree. */
    bool mountBuild();
    /** @brief Finishes the build pass and mounts as a named host. */
    bool mountBuildAs(const std::string &name);
    /** @brief Like mountBuildAs but reconciles by key when possible. */
    bool remountBuildAs(const std::string &name);

    /**
     * @brief Replace children of a Group `listId` with buttons for each label (reconcile).
     * Host must already exist and contain a group with that id.
     */
    bool setListItems(const std::string &listId, const std::vector<std::string> &items);

    /** @brief Widget state setters/getters on the current host (by node id). */
    void setText(const std::string &id, const std::string &text);
    /** @brief Sets the text wrap. */
    void setTextWrap(const std::string &id, float width);
    /** @brief Sets the visible. */
    void setVisible(const std::string &id, bool visible);
    /** @brief Enables or disables a mounted control. */
    void setEnabled(const std::string &id, bool enabled);
    /** @brief Sets the checked. */
    void setChecked(const std::string &id, bool checked);
    /** @brief Sets the value. */
    void setValue(const std::string &id, float value);
    /** @brief Sets the value text. */
    void setValueText(const std::string &id, const std::string &value);
    /** @brief Sets RGBA on a ColorPalette (or image tint) node. */
    void setColor(const std::string &id, float r, float g, float b, float a = 1.f);
    /** @brief Sets the image tint. */
    void setImageTint(const std::string &id, float r, float g, float b, float a = 1.f);
    /** @brief Sets the image uv. */
    void setImageUv(const std::string &id, float u0, float v0, float u1, float v1);
    /** @brief Sets the image nine patch. */
    void setImageNinePatch(const std::string &id, float l, float t, float r, float b);
    /** @brief Applies a .9.png texture and its parsed stretch metadata to an image. */
    bool setImageNinePatchFile(const std::string &id, const std::string &path);
    /** @brief Sets the image corner radius. */
    void setImageCornerRadius(const std::string &id, float radius);
    /** Bind a texture id from registerTexture() to an Image/ImageButton node. */
    /** @brief Sets the image texture id. */
    void setImageTextureId(const std::string &id, uint64_t textureId);
    /**
     * @brief Register an engine texture for UI drawing.
     * @param tex Live engine texture retained by the caller until unregisterTexture().
     * @return Opaque backend-neutral id, or zero on failure.
     */
    [[nodiscard("retain the UI texture registration id or explicitly handle failure")]]
    uint64_t registerTexture(graphics::Texture *tex);
    /**
     * @brief Release a texture id previously returned by registerTexture().
     * @param textureId Opaque backend-neutral texture id; zero is ignored.
     */
    void unregisterTexture(uint64_t textureId);
    /** @brief Returns the value. */
    float getValue(const std::string &id) const;
    /** @brief Returns the value text. */
    std::string getValueText(const std::string &id) const;
    /** @brief ColorPalette / image tint channels; missing nodes return 0. */
    float getColorR(const std::string &id) const;
    /** @brief Returns the color g. */
    float getColorG(const std::string &id) const;
    /** @brief Returns the color b. */
    float getColorB(const std::string &id) const;
    /** @brief Returns the color a. */
    float getColorA(const std::string &id) const;
    /** @brief Returns the checked. */
    bool getChecked(const std::string &id) const;
    /** @brief Requests keyboard/gamepad focus for a mounted control. */
    bool requestFocus(const std::string &id);
    /** @brief Moves focus by "next", "previous", "left", "right", "up", or "down". */
    bool moveFocus(const std::string &direction);
    /** @brief Returns the focused control id, or an empty string. */
    std::string getFocusedId() const;
    /** @brief Host-level state. */
    void setHostVisible(bool visible);
    /** @brief Sets the host layer. */
    void setHostLayer(int layer);
    /** @brief Marks the host as a modal (blocks other hosts) / overlay. */
    void setHostModal(bool modal);
    /** @brief Sets the host overlay. */
    void setHostOverlay(bool overlay);
    /** @brief Sets overlay opacity (0..1); zero also removes the host border and outer padding. */
    void setHostOverlayAlpha(float alpha);
    /** @brief Allows or prevents user movement after the initial position. */
    void setHostMovable(bool movable);
    /** @brief Allows or prevents user resizing after the initial size. */
    void setHostResizable(bool resizable);
    /** @brief Positions the host window with a pivot (0..1 each axis). */
    void setHostPos(float x, float y, float pivotX = 0.f, float pivotY = 0.f);
    /**
     * @brief Projects the selected host from a 3D world point through the active Camera3D.
     * @param x World X coordinate.
     * @param y World Y coordinate.
     * @param z World Z coordinate.
     */
    void setHostWorldAnchor(float x, float y, float z);
    /** @brief Returns the selected host to ordinary screen-space placement. */
    void clearHostWorldAnchor();
    /**
     * @brief Configures off-screen handling and safe-edge margin.
     * @param policy Either "hide" or "clamp".
     * @param safeMargin Minimum distance from the viewport edge in pixels.
     */
    void setHostWorldEdgePolicy(const std::string &policy, float safeMargin = 8.f);
    /**
     * @brief Configures distance scaling.
     * @param enabled Whether distance scaling is active.
     * @param referenceDistance Distance producing scale 1; must be positive.
     * @param minScale Lower scale bound.
     * @param maxScale Upper scale bound.
     */
    void setHostWorldDistanceScale(bool enabled, float referenceDistance = 10.f,
                                   float minScale = 0.65f, float maxScale = 1.25f);
    /**
     * @brief Configures deterministic overlap avoidance for projected hosts.
     * @param enabled Whether this host may be displaced to avoid overlap.
     * @param priority Higher values claim space first.
     * @param padding Extra separation around the measured host rectangle.
     * @param maxDisplacement Maximum vertical displacement in pixels.
     */
    void setHostWorldOverlap(bool enabled, int priority = 0, float padding = 4.f,
                             float maxDisplacement = 96.f);
    /** @brief Returns the selected host projection state as a stable lowercase name. @return State name. */
    std::string getHostWorldState() const;
    /** @brief Returns the selected host's derived screen X coordinate. @return Screen X in pixels. */
    float getHostWorldScreenX() const;
    /** @brief Returns the selected host's derived screen Y coordinate. @return Screen Y in pixels. */
    float getHostWorldScreenY() const;
    /** Host anchor in display (0..1); offsets come from setHostPos. */
    /** @brief Sets the host anchor. */
    void setHostAnchor(float x, float y);
    /** Explicit host window size (px). */
    /** @brief Sets the host size. */
    void setHostSize(float w, float h);
    /** Host window size as a fraction of the display (0..1). */
    /** @brief Sets the host percent. */
    void setHostPercent(float w, float h);
    /**
     * @brief Animate the selected host's window position (px).
     * @param x Target posX in pixels.
     * @param y Target posY in pixels.
     * @param durationMs Duration; <=0 jumps immediately (after delay).
     * @param ease Ease kind (default "smoothstep"; see evaluateUiEase).
     * @param delayMs Delay before the first sample moves.
     * @note Retrigger replaces any prior pos tween on the same host. Driven by
     *       the presentation clock inside beginFrameAndRender() / tickTweens().
     */
    void animateHostPos(float x, float y, float durationMs, const std::string &ease = "smoothstep",
                        float delayMs = 0.f);
    /**
     * @brief Animate the selected host's explicit window size (px).
     * @param w Target width.
     * @param h Target height.
     * @param durationMs Duration; <=0 jumps immediately (after delay).
     * @param ease Ease kind (default "smoothstep").
     * @param delayMs Delay before the first sample moves.
     */
    void animateHostSize(float w, float h, float durationMs, const std::string &ease = "smoothstep",
                         float delayMs = 0.f);
    /**
     * @brief Animate the selected host's overlay background alpha.
     * @param alpha Target alpha clamped to [0,1].
     * @param durationMs Duration; <=0 jumps immediately (after delay).
     * @param ease Ease kind (default "smoothstep").
     * @param delayMs Delay before the first sample moves.
     */
    void animateHostOverlayAlpha(float alpha, float durationMs, const std::string &ease = "smoothstep",
                                 float delayMs = 0.f);
    /**
     * @brief Smoothly animates one retained node's transient opacity without rebuilding its tree.
     * @param id Node id on the selected host.
     * @param opacity Target opacity clamped to [0,1].
     * @param durationMs Duration; <=0 jumps immediately (after delay).
     * @param ease Ease kind (default "smoothstep").
     * @param delayMs Delay before the first sample moves.
     */
    void animateItemOpacity(const std::string &id, float opacity, float durationMs,
                            const std::string &ease = "smoothstep", float delayMs = 0.f);
    /**
     * @brief Animate one node's absolute placement offset (marks the node absolute).
     * @param id Node id on the selected host.
     * @param x Target posX.
     * @param y Target posY.
     * @param durationMs Duration; <=0 jumps immediately (after delay).
     * @param ease Ease kind (default "smoothstep").
     * @param delayMs Delay before the first sample moves.
     */
    void animateItemPos(const std::string &id, float x, float y, float durationMs,
                        const std::string &ease = "smoothstep", float delayMs = 0.f);
    /** @brief Cancel pending host-level tweens on the selected host. */
    void cancelHostTweens();
    /**
     * @brief Cancel pending item tweens on the selected host.
     * @param id Node id; empty cancels every item tween on the host.
     */
    void cancelItemTweens(const std::string &id = "");
    /** @brief Cancel every pending tween on the selected host. */
    void cancelAllTweens();
    /**
     * @brief Advance pending UI tweens using an explicit clock (ms).
     * @param nowMs Monotonic presentation clock in milliseconds.
     * @note beginFrameAndRender() calls this with the wall clock; tests inject time here.
     */
    void tickTweens(double nowMs);
    /** @brief Number of pending host tweens (diagnostics / tests). */
    std::size_t getHostTweenCount() const;
    /** @brief Number of pending item tweens (diagnostics / tests). */
    std::size_t getItemTweenCount() const;
    /** @brief Returns the id of the clicked widget since the last frame (or ""). */
    std::string consumeClick();
    /** @brief Returns the id of the changed widget since the last frame (or ""). */
    std::string consumeChange();
    /** @brief Returns "supported" on desktop builds and "unsupported-platform" elsewhere. */
    std::string dragDropSupport() const;
    /** @brief Pops one drop and returns its target as "host/node", or an empty string. */
    std::string consumeDrop();
    /** @brief Returns the payload type of the last successfully consumed drop. */
    std::string getDropType() const;
    /** @brief Returns the owning payload text of the last successfully consumed drop. */
    std::string getDropText() const;
    /** @brief Returns the source as "host/node", or empty for an OS file. */
    std::string getDropSource() const;
    /** @brief Returns "internal", "os-file", or an empty string. */
    std::string getDropOrigin() const;

    /**
     * Script-side event callbacks (P0-3): register a closure on a node id of
     * the selected host. onClick registers a callback for a node; onChange
     * registers a value callback whose kind is toggle, value or text. Handlers
     * run inside dispatchEvents after C++ callbacks and before poll queues.
     */
    /** @brief On click. */
    void onClick(const std::string &id, ssq::Function fn);
    /** @brief On change. */
    void onChange(const std::string &id, ssq::Function fn);
    /** @brief Internal UIComponent callback registration scoped to one component owner. */
    void componentOnClick(uint64_t owner, const std::string &id, ssq::Function fn);
    /** @brief Internal UIComponent value callback registration scoped to one component owner. */
    void componentOnChange(uint64_t owner, const std::string &id, ssq::Function fn);
    /** @brief Removes every callback owned by one UIComponent; idempotent on the UI thread. */
    void componentClearHandlers(uint64_t owner);

    /** @brief Applies the dark/light built-in theme. */
    void setThemeDark();
    /** @brief Sets the theme light. */
    void setThemeLight();
    /** @brief Named preset: "dark" / "light" (case-insensitive). Returns false if unknown. */
    bool setTheme(const std::string &name);
    /** @brief Name of the active theme. */
    std::string getTheme() const;
    /** @brief Declares/replaces a named style class and returns a stable status string. */
    std::string defineStyleClass(const std::string &name, const std::string &parent = "");
    /** @brief Sets a named class color token and returns a stable status string. */
    std::string setStyleClassColor(const std::string &name, const std::string &property, float r,
                                   float g, float b, float a = 1.f);
    /** @brief Sets a named class metric token and returns a stable status string. */
    std::string setStyleClassMetric(const std::string &name, const std::string &property, float x,
                                    float y = 0.f);
    /** @brief Clears all named style classes. */
    void clearStyleClasses();
    /** @brief Enables/disables keyboard navigation support. */
    void setNavKeyboard(bool enabled);
    /** @brief Sets the nav gamepad. */
    void setNavGamepad(bool enabled);
    /** @brief Global UI scale factor (default 1). */
    void setScale(float scale);
    /** @brief Current UI scale factor. */
    float getScale() const;
    /** "hosts=.. nodes=.. measureMs=.. walkMs=.." from the last render frame. */
    /** @brief Returns the stats. */
    std::string getStats() const;
    /** @brief JSON diagnostics for measured size and overflow of the selected retained tree. */
    std::string getLayoutDiagnostics() const;
    /** @brief JSON accessibility snapshot for semantically named nodes in the selected tree. */
    std::string getAccessibilitySnapshot() const;
    /** Serialize the selected host's tree to JSON (UI asset pipeline). */
    /** @brief Saves tree json. */
    std::string saveTreeJson() const;
    /** Replace the selected host's tree from JSON produced by saveTreeJson(). */
    /** @brief Loads tree json. */
    bool loadTreeJson(const std::string &json);
    /**
     * @brief Returns the offscreen render target of a selected-host Viewport widget.
     * @return Borrowed nullable Canvas owned by Graphics; null precedes layout or when the id is absent.
     * @ownership Graphics owns the canvas; UI only registers and presents it.
     * @lifetime Valid until viewport recreation, graphics reset, or the next mutation that replaces the target; copy no
     * pointer across frames.
     * @thread Call on the render/UI thread.
     * @reentrancy The query invokes no callbacks; do not destroy or replace the target re-entrantly.
     */
    graphics::Canvas *viewportCanvas(const std::string &id);
    /** @brief Viewport hovered. */
    bool viewportHovered(const std::string &id);
    /** @brief Viewport active. */
    bool viewportActive(const std::string &id);
    /** @brief Viewport mouse x. */
    float viewportMouseX(const std::string &id);
    /** @brief Viewport mouse y. */
    float viewportMouseY(const std::string &id);
    /** @brief Viewport drag dx. */
    float viewportDragDX(const std::string &id);
    /** @brief Viewport drag dy. */
    float viewportDragDY(const std::string &id);
    /** @brief Viewport wheel. */
    float viewportWheel(const std::string &id);

    /** @brief One-shot convenience: a window with a label and a button. */
    void mountSimple(const std::string &title, const std::string &labelText,
                     const std::string &buttonText);

    // ---- Reflection-driven property inspector (MVVM DevTools) ---------------
    /** @brief Opens the auto-generated inspector (scans reflected classes). */
    bool inspectOpen();
    /** @brief Closes the inspector panel. */
    void inspectClose();
    /** @brief Re-scans script classes; true when any class is reflected. */
    bool inspectRefresh();
    /** @brief Selects a class in the inspector (creates its first instance). */
    bool inspectSelectClass(const std::string &name);
    /** @brief Inspects a caller-provided live script instance. */
    bool inspectObject(ssq::Object object);
    /**
     * @brief Registers the script callback used by the inspector Pick button.
     * The callback is stored on the script side (eve._inspectorPickHandler) and
     * must return a live script instance (or null).
     */
    bool inspectSetPickHandler(ssq::Function fn);
    /** @brief Calls the pick handler and inspects the returned object. */
    bool inspectPickScene();
    /** @brief Creates another instance of the selected inspector class. */
    bool inspectAddInstance();
    /**
     * @brief Return the reflection-derived `PropertySchema` for a live script instance.
     *
     * The schema is built by `ReflectedPropertyModel` (attributes → kinds/flags) and
     * projected as a Squirrel table `{ typeId, version, properties = [ { path, kind,
     * displayName, description, category, readOnly, choices, min, max, step, units,
     * presenterHint }, ... ] }`. Scripts use this to inspect or drive custom UI; the
     * authoritative editable view remains `ui.inspectObject` / PropertyView.
     */
    ssq::Object propertySchema(ssq::Object instance);

    // ---- Reflection-driven database panel -------------------------------
    /** @brief Opens the database panel (class menu + editable instance grid). */
    bool dbOpen();
    /** @brief Closes the database panel. */
    void dbClose();
    /** @brief Re-scans reflected classes; true when any class exists. */
    bool dbRefresh();
    /** @brief Selects the class shown in the grid. */
    bool dbSelectClass(const std::string &name);
    /**
     * @brief Registers a live script object in the database grid.
     * @param object Live script instance.
     * @param label  Optional display label ("" = auto "Class #n").
     * @return Explicit packed process-local ObjectHandle, or 0 on failure.
     * @remarks This integer is a Squirrel compatibility projection. C++ code
     *          should use ui::ObjectHandle from ObjectRegistry directly; it
     *          is not a persistent ID.
     */
    [[nodiscard("retain the packed UI object handle or explicitly handle failure")]]
    uint64_t dbRegister(ssq::Object object, const std::string &label);
    /**
     * @brief Creates and registers an instance of the selected class.
     * @return Explicit packed process-local ObjectHandle, or 0 on failure.
     */
    [[nodiscard("retain the packed UI object handle or explicitly handle failure")]]
    uint64_t dbCreateInstance();
    /**
     * @brief Removes an entry from the database grid using its packed handle.
     * @param id Process-local generation-qualified ObjectHandle packed for the
     *            Squirrel-facing API.
     * @return Applied on success, or a structured failure when the panel is
     *         unavailable or the handle is invalid/stale.
     * @thread Call on the UI thread.
     */
    [[nodiscard("inspect the database removal result or explicitly ignore it")]]
    eve::Result<void> dbUnregister(uint64_t id);

    // ---- DevTools editor shell --------------------------------------------
    /** @brief Opens the menu bar and docks the inspector + database panels. */
    bool editorOpen();
    /** @brief Closes the editor shell (and its docked panels). */
    void editorClose();
    /** @brief Shows one docked panel ("inspector"/"database"; "" hides all). */
    bool editorSelectPanel(const std::string &name);

    // ---- Scene hierarchy panel --------------------------------------------
    /** @brief Opens the scene panel (tree + selected node properties). */
    bool sceneOpen();
    /** @brief Closes the scene panel. */
    void sceneClose();
    /** @brief Selects a scene node by id. */
    bool sceneSelectNode(const std::string &id);
    /**
     * @brief Registers the script callback for the scene panel Pick button.
     * The callback (stored as eve._scenePickHandler) receives the node id and
     * typically maps it to a script instance, then calls ui.inspectObject().
     */
    bool sceneSetPickHandler(ssq::Function fn);

private:
    struct NinePatchResource {
        graphics::Texture *texture = nullptr;
        uint64_t textureId = 0;
        NinePatchInfo info;
    };
    /**
     * @brief Loads or finds a nine-patch resource for one synchronous build operation.
     * @return Borrowed nullable cache entry owned by UI.
     * @ownership UI owns the cache entry; Graphics owns its texture.
     * @lifetime Valid until the cache mutates, releaseNinePatches(), or UI destruction.
     * @thread Call on the UI/render thread.
     * @reentrancy The loader does not invoke external callbacks while returning the entry.
     */
    NinePatchResource *loadNinePatch(const std::string &path);
    void releaseNinePatches();

    WidgetDesc &currentParent();
    void pushOpen(WidgetDesc d);
    bool buildComplete() const;
    /**
     * @brief Resolves or creates the selected host for one UI operation.
     * @return Borrowed nullable UIHost owned by the UI ECS world.
     * @ownership UI/ECS owns the host; callers must not delete it.
     * @lifetime Valid until host removal, replacement, or UI module teardown.
     * @thread Call on the UI thread.
     * @reentrancy The helper may perform host selection but invokes no external callback while returning.
     */
    UIHostHandle ensureSelected(const std::string &preferredName = "");
    /**
     * @brief Resolves the selected host for one synchronous UI operation.
     * @return A borrowed reference when the generation-qualified handle is live.
     * @lifetime The reference is valid only for this synchronous operation.
     */
    [[nodiscard]] eve::OptionalRef<UIHost> resolveSelected() const noexcept;

    std::unique_ptr<UIBackend> backend_;
    std::unordered_map<std::string, NinePatchResource> ninePatches_;
    UIHostHandle                                       selected_{};

    std::vector<WidgetDesc> openStack_;
    WidgetDesc builtRoot_;
    bool hasBuiltRoot_ = false;
    std::string lastDropType_;
    std::string lastDropText_;
    std::string lastDropSource_;
    std::string lastDropOrigin_;

    struct ScriptHandler {
        /** @brief Constructs a ScriptHandler. */
        ScriptHandler(std::string host, std::string node, std::string k, ssq::Function f,
                      uint64_t componentOwner = 0)
            /** @brief Host name. */
            : hostName(std::move(host)), nodeId(std::move(node)), kind(std::move(k)),
              /** @brief Fn. */
              fn(std::move(f)), owner(componentOwner) {}
        std::string hostName;
        std::string nodeId;
        std::string kind;  // "click" | "toggle" | "value" | "text"
        ssq::Function fn;
        uint64_t owner = 0;  // 0 belongs to the legacy host-level registration API.
    };
    std::vector<ScriptHandler> scriptHandlers_;
    void fireScriptHandlers(const UIEvent &ev);

    UiTweenDriver                  tweens_;
    ssq::Object callPickHandler();
    void callScenePickHandler(const std::string &nodeId);
    std::unique_ptr<Inspector> inspector_;
    std::unique_ptr<DatabasePanel> databasePanel_;
    std::unique_ptr<EditorShell> editorShell_;
    std::unique_ptr<ScenePanel> scenePanel_;
};

}  // namespace eve::ui
