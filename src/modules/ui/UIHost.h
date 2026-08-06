#pragma once

#include "common/ECS.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve::ui {

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
};

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
    float sizeX = 0.f;  // Child size; 0 = auto
    float sizeY = 0.f;
    int firstChild = -1;
    int nextSibling = -1;
    uint32_t handlerClick = 0;   // 1-based → Tree::clickHandlers
    uint32_t handlerToggle = 0;  // 1-based → Tree::toggleHandlers
    uint32_t handlerValue = 0;   // 1-based → Tree::valueHandlers
    uint32_t handlerText = 0;    // 1-based → Tree::textHandlers
};

struct WidgetDesc;

/**
 * ECS mount point for one UI panel/screen.
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

    void setName(const std::string &name);
    const std::string &getName();
    void setOwnerId(uint32_t id) { meta()->ownerId = id; }
    uint32_t getOwnerId() { return meta()->ownerId; }

    /** Full replace. */
    void setTree(WidgetDesc root);
    /** Key-aware patch when structure matches; else full replace. */
    bool setTreeReconcile(WidgetDesc root);

    void setSimplePanel(const std::string &title, const std::string &labelText,
                        const std::string &buttonText, const std::string &labelId = "label",
                        const std::string &buttonId = "btn");

    void setTextById(const std::string &id, const std::string &text);
    void setVisibleById(const std::string &id, bool visible);
    void setCheckedById(const std::string &id, bool checked);
    void setValueById(const std::string &id, float value);
    void setValueTextById(const std::string &id, const std::string &value);
    bool setClickHandler(const std::string &id, std::function<void()> fn);
    bool setToggleHandler(const std::string &id, std::function<void(bool)> fn);
    bool setValueHandler(const std::string &id, std::function<void(float)> fn);
    bool setTextHandler(const std::string &id, std::function<void(const std::string &)> fn);
    UINode *findById(const std::string &id);
    UINode *findByKey(const std::string &key);
    void markDirty() { tree()->dirty = true; }

    void setVisible(bool v) { meta()->visible = v; }
    void setLayer(int layer) { meta()->layer = layer; }
    void setModal(bool modal) { meta()->modal = modal; }
};

}  // namespace eve::ui
