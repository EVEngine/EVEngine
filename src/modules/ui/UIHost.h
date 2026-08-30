#pragma once

#include "common/BorrowedRef.h"
#include "common/ECS.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve::ui {

using UIHostHandle = ecs::EntityHandle;

/** @brief Widget node kinds understood by the UI renderer. */
enum class NodeType : uint8_t {
    Window = 0,
    Text = 1,
    Button = 2,
    SameLine = 3,
    Group = 4,
    Separator = 5,
    Checkbox = 6,
    Slider = 7,
    Progress = 8,
    InputText = 9,
    CollapsingHeader = 10,
    Child = 11,
    Flex = 12,    // elastic row/column container
    Spacer = 13,  // flexible empty space (default flexGrow=1)
    Image = 14,   // colored rect or engine texture (nine-patch capable)
    ImageButton = 15,  // clickable image
    Combo = 16,   // dropdown; options newline-separated in valueText, index in value
    ScrollList = 17,  // virtualized scrollable list (uniform itemHeight)
    Viewport = 18,  // embedded render target: offscreen Canvas shown + input routed
    SearchField = 19,   // compact input with a search hint/icon
    Switch = 20,        // modern boolean toggle
    Badge = 21,         // compact status/category pill
    Card = 22,          // bordered surface container
    SectionHeader = 23, // non-collapsible editor section heading
    MenuBar = 24,       // native ImGui window menu bar
    Menu = 25,          // popup menu container
    MenuItem = 26,      // selectable menu command
    Toolbar = 27,       // horizontal editor command strip
    Toolbox = 28,       // wrapping grid of editor tools
    Sidebar = 29,       // vertical editor side panel
    StatusBar = 30,     // compact horizontal status strip
    SplitPane = 31,      // two resizable panes
    NinePatchPanel = 32, // .9.png-backed stretchable content container
};

/** @brief Main-axis direction for Flex containers. */
enum class FlexDirection : uint8_t { Row = 0, Column = 1 };

/** @brief Cross-axis alignment of Flex children. */
enum class FlexAlign : uint8_t { Start = 0, Center = 1, End = 2, Stretch = 3 };

/** @brief Main-axis distribution of free space in a Flex container. */
enum class FlexJustify : uint8_t {
    Start = 0,
    Center = 1,
    End = 2,
    SpaceBetween = 3,
    SpaceAround = 4,
};

/** @brief Keyboard/gamepad focus policy for an interactive control. */
enum class FocusMode : uint8_t { None = 0, Click = 1, All = 2 };

/** @brief Logical focus movement understood by keyboard, gamepad and automation. */
enum class FocusDirection : uint8_t { Next = 0, Previous, Left, Right, Up, Down };

/** @brief Pointer event participation and propagation policy. */
enum class MouseFilter : uint8_t { Stop = 0, Pass = 1, Ignore = 2 };

/** @brief Built-in theme override inherited by a retained UI subtree. */
enum class ThemePreset : uint8_t { Inherit = 0, Dark = 1, Light = 2 };

/** @brief Visibility result of projecting a world-space UI anchor. */
enum class WorldAnchorState : uint8_t {
    Disabled = 0,
    Visible,
    BehindCamera,
    OutsideViewport,
    NoCamera,
    Crowded
};

/** @brief Policy used when a projected world-space anchor leaves the viewport. */
enum class WorldAnchorEdgePolicy : uint8_t { Hide = 0, Clamp };

/** @brief Deterministic screen-space overlap policy for projected world anchors. */
enum class WorldAnchorOverlapPolicy : uint8_t { Allow = 0, Avoid };

/** @brief Platform-neutral accessibility role exposed by controls. */
enum class AccessibilityRole : uint8_t {
    Auto = 0,
    Button,
    Checkbox,
    Slider,
    Text,
    TextInput,
    List,
    ListItem,
    Menu,
    MenuItem,
    Progress,
    Region,
    Tab,
    Window
};

/**
 * @brief Retained UI widget node (arena tree). Conceptual counterpart of
 * eve::scene::SceneNode; built declaratively from WidgetDesc.
 */
struct UINode {
    NodeType type = NodeType::Text;
    std::string id;
    std::string key;
    std::string text;       // label / title
    std::string valueText;  // InputText content
    std::string tooltip;    // hover help; empty disables the tooltip
    bool visible = true;
    bool enabled = true;
    bool checked = false;
    bool open = true;  // CollapsingHeader default-open hint
    FocusMode focusMode = FocusMode::All;
    MouseFilter mouseFilter = MouseFilter::Stop;
    ThemePreset themePreset = ThemePreset::Inherit;
    int tabIndex = 0;
    std::string focusNext;
    std::string focusPrevious;
    std::string focusLeft;
    std::string focusRight;
    std::string focusUp;
    std::string focusDown;
    bool focusRequested = false;
    bool focused = false;
    AccessibilityRole accessibilityRole = AccessibilityRole::Auto;
    std::string accessibilityName;
    std::string accessibilityDescription;
    bool dragSource = false;
    bool dropTarget = false;
    std::string dragPayloadType;
    std::string dragPayloadText;
    std::string acceptedDropType;
    float value = 0.f;
    float minValue = 0.f;
    float maxValue = 1.f;
    float sizeX = 0.f;  // Child / item basis width; 0 = auto
    float sizeY = 0.f;  // Child / item basis height; 0 = auto
    // --- Layout box model (input props; 0 = unset) ---
    float marginL = 0.f;
    float marginT = 0.f;
    float marginR = 0.f;
    float marginB = 0.f;
    float paddingL = 0.f;
    float paddingT = 0.f;
    float paddingR = 0.f;
    float paddingB = 0.f;
    float minSizeX = 0.f;  // 0 = auto
    float minSizeY = 0.f;
    float maxSizeX = 0.f;  // 0 = unlimited
    float maxSizeY = 0.f;
    float percentW = 0.f;  // 0..1 fraction of parent content width; overrides basis
    float percentH = 0.f;  // 0..1 fraction of parent content height
    float anchorX = 0.f;   // absolute placement inside Flex: anchor point in parent (0..1)
    float anchorY = 0.f;
    float posX = 0.f;      // absolute placement offset (relative to anchor edge)
    float posY = 0.f;
    bool absolute = false; // skip flex flow; placed by anchor/pos
    // --- Image / ImageButton props (NodeType::Image*) ---
    uint64_t textureId = 0;  // opaque id from UIBackend::registerTexture; 0 = solid color
    float tintR = 1.f;
    float tintG = 1.f;
    float tintB = 1.f;
    float tintA = 1.f;
    float uv0x = 0.f;  // source rect in texture (UV)
    float uv0y = 0.f;
    float uv1x = 1.f;
    float uv1y = 1.f;
    float borderL = 0.f;  // nine-patch border (px); all 0 = plain image
    float borderT = 0.f;
    float borderR = 0.f;
    float borderB = 0.f;
    float cornerRadius = 0.f;  // solid-color rounded rect radius
    float wrapWidth = 0.f;     // Text wrap width (0 = no wrap)
    float itemHeight = 0.f;    // ScrollList uniform row height (0 = frame height)
    // --- Computed by measure/arrange pass (per frame) ---
    float measuredW = 0.f;
    float measuredH = 0.f;
    // Flex container props (meaningful on NodeType::Flex)
    FlexDirection flexDirection = FlexDirection::Row;
    FlexAlign alignItems = FlexAlign::Start;
    FlexJustify justifyContent = FlexJustify::Start;
    float gap = -1.f;  // <0 → theme ItemSpacing on that axis
    // Flex item props (any child inside Flex)
    float flexGrow = 0.f;
    int parent = -1;
    int firstChild = -1;
    int nextSibling = -1;
    uint32_t handlerClick = 0;   // 1-based → Tree::clickHandlers
    uint32_t handlerToggle = 0;  // 1-based → Tree::toggleHandlers
    uint32_t handlerValue = 0;   // 1-based → Tree::valueHandlers
    uint32_t handlerText = 0;    // 1-based → Tree::textHandlers
};

struct WidgetDesc;

/**
 * @brief ECS mount point for one UI panel/screen.
 * Subclass to attach UI to game entities, e.g. `class Hud : public UIHost`.
 * UISystem walks `ecs::View<UIHost, Meta, Tree>` (includes subclasses).
 */
class UIHost : public ecs::Entity {
public:
    ENTITY(UIHost, ecs::Entity)

    void release() override {}

    struct Meta {
        bool visible = true;
        int layer = 0;
        bool modal = false;
        bool overlay = false;  // no title bar / chrome (HUD-style)
        bool hasPos = false;
        bool lockPos = true;       // false: initial position only; user may move/persist it
        float posX = 0.f;
        float posY = 0.f;
        float pivotX = 0.f;
        float pivotY = 0.f;
        bool hasSize = false;      // explicit window size
        bool lockSize = true;      // false: initial size only; user may resize/persist it
        float sizeX = 0.f;
        float sizeY = 0.f;
        float percentW = 0.f;      // 0..1 of display width; overrides sizeX
        float percentH = 0.f;      // 0..1 of display height
        float anchorX = 0.f;       // anchor in display (0..1); with hasPos, posX is offset
        float anchorY = 0.f;
        std::string name;
        float overlayBgAlpha = 0.4f;
        bool overlayFlush = false;  // remove outer WindowPadding for desktop chrome
        uint32_t ownerId = 0;
        UIHostHandle entity{};
    };

    struct Tree {
        bool dirty = true;
        std::vector<UINode> nodes;
        int root = -1;
        std::vector<std::function<void()>> clickHandlers;
        std::vector<std::function<void(bool)>> toggleHandlers;
        std::vector<std::function<void(float)>> valueHandlers;
        std::vector<std::function<void(const std::string &)>> textHandlers;
    };

    /**
     * @brief Optional 3D-world projection for this UI host.
     *
     * The world position and policy are authoritative input owned by UIHost. The resolved
     * screen position/state are transient derived data refreshed by UISystem on the render
     * thread. The link is value-based: destroying a scene object does not leave a pointer;
     * its owner must stop updating or disable this anchor. Camera removal is detected every
     * frame and reported as NoCamera. Restore/hot reload rebuilds the link from worldPosition.
     */
    struct WorldAnchor {
        bool enabled = false;
        float worldX = 0.f;
        float worldY = 0.f;
        float worldZ = 0.f;
        float offsetX = 0.f;
        float offsetY = 0.f;
        float safeMargin = 8.f;
        WorldAnchorEdgePolicy edgePolicy = WorldAnchorEdgePolicy::Hide;
        bool hideBehindCamera = true;
        bool distanceScale = false;
        float referenceDistance = 10.f;
        float minScale = 0.65f;
        float maxScale = 1.25f;
        WorldAnchorOverlapPolicy overlapPolicy = WorldAnchorOverlapPolicy::Allow;
        int overlapPriority = 0;
        float overlapPadding = 4.f;
        float maxDisplacement = 96.f;

        WorldAnchorState state = WorldAnchorState::Disabled;
        float screenX = 0.f;
        float screenY = 0.f;
        float depth = 0.f;
        float scale = 1.f;
        float displacementX = 0.f;
        float displacementY = 0.f;
    };

    COMPONENT(Meta, meta)
    COMPONENT(Tree, tree)
    COMPONENT(WorldAnchor, worldAnchor)

    /**
     * @brief Creates an ECS-owned UI host.
     * @return A generation-checked handle; an empty handle means creation failed.
     * @ownership The UI ECS world owns the host; callers must not delete it.
     * @lifetime The handle is valid until ECS destruction or UI module teardown; resolve it at each use.
     * @thread Call on the UI ECS thread.
     * @reentrancy The factory invokes no external callbacks.
     */
    [[nodiscard]] static UIHostHandle createHost(const std::string &name = "");

    /**
     * @brief Resolves a UI host handle with ECS generation checking.
     * @param handle Candidate host handle.
     * @return A temporary borrowed host for the current UI operation, or null when stale.
     * @ownership The UI ECS world owns the returned host; callers must not delete it.
     * @lifetime Valid only until the next UI ECS structural mutation or world teardown.
     * @thread Call on the UI ECS thread.
     * @reentrancy Does not invoke callbacks; do not retain the returned pointer across frames.
     */
    [[nodiscard]] static eve::OptionalRef<UIHost> resolve(UIHostHandle handle) noexcept;

    /**
     * @brief Returns this host's generation-qualified ECS handle.
     * @return A handle that becomes stale when this host is destroyed or replaced.
     */
    [[nodiscard]] UIHostHandle handle() const noexcept { return ecs::handle_of(this); }

    /** @brief Names the host. */
    void setName(const std::string &name);
    const std::string &getName();
    /** @brief Attaches the host to an owner id (scene/UI ownership). */
    void setOwnerId(uint32_t id) { meta()->ownerId = id; }
    uint32_t getOwnerId() { return meta()->ownerId; }

    /** @brief Full replace. */
    void setTree(WidgetDesc root);
    /** @brief Key-aware patch when structure matches; else full replace. */
    bool setTreeReconcile(WidgetDesc root);

    /** @brief One-shot convenience panel: window + label + button. */
    void setSimplePanel(const std::string &title, const std::string &labelText,
                        const std::string &buttonText, const std::string &labelId = "label",
                        const std::string &buttonId = "btn");

    /** @brief Widget state updates by node id. */
    void setTextById(const std::string &id, const std::string &text);
    void setVisibleById(const std::string &id, bool visible);
    void setEnabledById(const std::string &id, bool enabled);
    void setCheckedById(const std::string &id, bool checked);
    void setValueById(const std::string &id, float value);
    void setValueTextById(const std::string &id, const std::string &value);
    /** @brief Installs a widget callback by node id (returns false if not found). */
    bool setClickHandler(const std::string &id, std::function<void()> fn);
    bool setToggleHandler(const std::string &id, std::function<void(bool)> fn);
    bool setValueHandler(const std::string &id, std::function<void(float)> fn);
    bool setTextHandler(const std::string &id, std::function<void(const std::string &)> fn);
    /**
     * @brief Looks up a node by stable id without transferring ownership.
     * @return Borrowed nullable node owned by this host's tree.
     * @ownership UIHost owns the node tree; callers must not delete the result.
     * @lifetime Valid until the next tree replacement/reconcile or host destruction.
     * @thread Call on the UI thread owning this host.
     * @reentrancy Do not retain across callbacks or tree mutation.
     */
    [[nodiscard]] eve::OptionalRef<UINode> findById(const std::string &id);
    /**
     * @brief Looks up a node by reconciliation key without transferring ownership.
     * @return Borrowed nullable node owned by this host's tree.
     * @ownership UIHost owns the node tree; callers must not delete the result.
     * @lifetime Valid until the next tree replacement/reconcile or host destruction.
     * @thread Call on the UI thread owning this host.
     * @reentrancy Do not retain across callbacks or tree mutation.
     */
    [[nodiscard]] eve::OptionalRef<UINode> findByKey(const std::string &key);
    /** @brief Request keyboard/gamepad focus for a control on the next frame. */
    bool requestFocusById(const std::string &id);
    /**
     * @brief Move through explicit neighbors or deterministic tab order.
     * @param direction Logical sequential or directional movement.
     * @param wrap Whether sequential fallback wraps at the first/last control.
     */
    bool moveFocus(FocusDirection direction, bool wrap = true);
    /** @brief Return the currently focused control id, or an empty string. */
    std::string focusedId();
    /** @brief Marks the tree for a full rebuild on the next frame. */
    void markDirty() { tree()->dirty = true; }

    /** @brief Host visibility / layer / modality. */
    void setVisible(bool v) { meta()->visible = v; }
    void setLayer(int layer) { meta()->layer = layer; }
    void setModal(bool modal) { meta()->modal = modal; }

    /**
     * @brief Enables projection of this host from a 3D world point.
     * @param x World X coordinate.
     * @param y World Y coordinate.
     * @param z World Z coordinate.
     */
    void setWorldAnchor(float x, float y, float z);
    /** @brief Disables 3D projection and returns the host to normal screen layout. */
    void clearWorldAnchor();
};

}  // namespace eve::ui
