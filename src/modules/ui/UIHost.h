#pragma once

#include "common/ECS.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve::ui {

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
    bool visible = true;
    bool checked = false;
    bool open = true;  // CollapsingHeader default-open hint
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
        float posX = 0.f;
        float posY = 0.f;
        float pivotX = 0.f;
        float pivotY = 0.f;
        bool hasSize = false;      // explicit window size
        float sizeX = 0.f;
        float sizeY = 0.f;
        float percentW = 0.f;      // 0..1 of display width; overrides sizeX
        float percentH = 0.f;      // 0..1 of display height
        float anchorX = 0.f;       // anchor in display (0..1); with hasPos, posX is offset
        float anchorY = 0.f;
        std::string name;
        uint32_t ownerId = 0;
        UIHost *entity = nullptr;
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

    COMPONENT(Meta, meta)
    COMPONENT(Tree, tree)

    static UIHost *createHost(const std::string &name = "");

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
    void setCheckedById(const std::string &id, bool checked);
    void setValueById(const std::string &id, float value);
    void setValueTextById(const std::string &id, const std::string &value);
    /** @brief Installs a widget callback by node id (returns false if not found). */
    bool setClickHandler(const std::string &id, std::function<void()> fn);
    bool setToggleHandler(const std::string &id, std::function<void(bool)> fn);
    bool setValueHandler(const std::string &id, std::function<void(float)> fn);
    bool setTextHandler(const std::string &id, std::function<void(const std::string &)> fn);
    /** @brief Looks up a node by id or reconciliation key. */
    UINode *findById(const std::string &id);
    UINode *findByKey(const std::string &key);
    /** @brief Marks the tree for a full rebuild on the next frame. */
    void markDirty() { tree()->dirty = true; }

    /** @brief Host visibility / layer / modality. */
    void setVisible(bool v) { meta()->visible = v; }
    void setLayer(int layer) { meta()->layer = layer; }
    void setModal(bool modal) { meta()->modal = modal; }
};

}  // namespace eve::ui
