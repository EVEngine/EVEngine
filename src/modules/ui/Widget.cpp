#include "ui/Widget.h"

namespace eve::ui {
namespace {

int appendNode(UIHost::Tree &tree, WidgetDesc &&desc) {
    const int index = int(tree.nodes.size());
    UINode node;
    node.type = desc.type;
    node.id = std::move(desc.id);
    node.key = desc.key.empty() ? node.id : std::move(desc.key);
    node.text = std::move(desc.text);
    node.valueText = std::move(desc.valueText);
    node.tooltip = std::move(desc.tooltip);
    node.visible = desc.visible;
    node.checked = desc.checked;
    node.open = desc.open;
    node.value = desc.value;
    node.minValue = desc.minValue;
    node.maxValue = desc.maxValue;
    node.sizeX = desc.sizeX;
    node.sizeY = desc.sizeY;
    node.marginL = desc.marginL;
    node.marginT = desc.marginT;
    node.marginR = desc.marginR;
    node.marginB = desc.marginB;
    node.paddingL = desc.paddingL;
    node.paddingT = desc.paddingT;
    node.paddingR = desc.paddingR;
    node.paddingB = desc.paddingB;
    node.minSizeX = desc.minSizeX;
    node.minSizeY = desc.minSizeY;
    node.maxSizeX = desc.maxSizeX;
    node.maxSizeY = desc.maxSizeY;
    node.percentW = desc.percentW;
    node.percentH = desc.percentH;
    node.anchorX = desc.anchorX;
    node.anchorY = desc.anchorY;
    node.posX = desc.posX;
    node.posY = desc.posY;
    node.absolute = desc.absolute;
    node.textureId = desc.textureId;
    node.tintR = desc.tintR;
    node.tintG = desc.tintG;
    node.tintB = desc.tintB;
    node.tintA = desc.tintA;
    node.uv0x = desc.uv0x;
    node.uv0y = desc.uv0y;
    node.uv1x = desc.uv1x;
    node.uv1y = desc.uv1y;
    node.borderL = desc.borderL;
    node.borderT = desc.borderT;
    node.borderR = desc.borderR;
    node.borderB = desc.borderB;
    node.cornerRadius = desc.cornerRadius;
    node.wrapWidth = desc.wrapWidth;
    node.itemHeight = desc.itemHeight;
    node.flexDirection = desc.flexDirection;
    node.alignItems = desc.alignItems;
    node.justifyContent = desc.justifyContent;
    node.gap = desc.gap;
    node.flexGrow = desc.flexGrow;
    node.firstChild = -1;
    node.nextSibling = -1;
    node.handlerClick = 0;
    node.handlerToggle = 0;
    node.handlerValue = 0;
    node.handlerText = 0;

    if (desc.onClick) {
        tree.clickHandlers.push_back(std::move(desc.onClick));
        node.handlerClick = static_cast<uint32_t>(tree.clickHandlers.size());
    }
    if (desc.onToggle) {
        tree.toggleHandlers.push_back(std::move(desc.onToggle));
        node.handlerToggle = static_cast<uint32_t>(tree.toggleHandlers.size());
    }
    if (desc.onValue) {
        tree.valueHandlers.push_back(std::move(desc.onValue));
        node.handlerValue = static_cast<uint32_t>(tree.valueHandlers.size());
    }
    if (desc.onTextChange) {
        tree.textHandlers.push_back(std::move(desc.onTextChange));
        node.handlerText = static_cast<uint32_t>(tree.textHandlers.size());
    }

    tree.nodes.push_back(std::move(node));

    int prevChild = -1;
    int firstChild = -1;
    for (auto &child : desc.children) {
        int childIndex = appendNode(tree, std::move(child));
        if (firstChild < 0) firstChild = childIndex;
        if (prevChild >= 0) tree.nodes[size_t(prevChild)].nextSibling = childIndex;
        prevChild = childIndex;
    }
    tree.nodes[size_t(index)].firstChild = firstChild;
    return index;
}

bool structureMatches(const UIHost::Tree &tree, int nodeIndex, const WidgetDesc &desc) {
    if (nodeIndex < 0 || nodeIndex >= int(tree.nodes.size())) return false;
    const UINode &n = tree.nodes[size_t(nodeIndex)];
    if (n.type != desc.type) return false;
    const std::string &dk = desc.reconcileKey();
    if (!dk.empty() && !n.key.empty() && n.key != dk) return false;

    std::vector<std::string> oldKeys;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        oldKeys.push_back(tree.nodes[size_t(c)].key);
    }
    if (oldKeys.size() != desc.children.size()) return false;
    for (size_t i = 0; i < desc.children.size(); ++i) {
        const std::string &ck = desc.children[i].reconcileKey();
        if (ck.empty() || oldKeys[i].empty()) {
            if (!(ck.empty() && oldKeys[i].empty())) return false;
        } else if (ck != oldKeys[i]) {
            return false;
        }
    }

    int child = n.firstChild;
    for (const auto &ch : desc.children) {
        if (!structureMatches(tree, child, ch)) return false;
        child = tree.nodes[size_t(child)].nextSibling;
    }
    return true;
}

void patchProps(UIHost::Tree &tree, int nodeIndex, WidgetDesc &&desc) {
    UINode &n = tree.nodes[size_t(nodeIndex)];
    n.text = std::move(desc.text);
    n.valueText = std::move(desc.valueText);
    n.tooltip = std::move(desc.tooltip);
    n.visible = desc.visible;
    n.checked = desc.checked;
    n.open = desc.open;
    n.value = desc.value;
    n.minValue = desc.minValue;
    n.maxValue = desc.maxValue;
    n.sizeX = desc.sizeX;
    n.sizeY = desc.sizeY;
    n.marginL = desc.marginL;
    n.marginT = desc.marginT;
    n.marginR = desc.marginR;
    n.marginB = desc.marginB;
    n.paddingL = desc.paddingL;
    n.paddingT = desc.paddingT;
    n.paddingR = desc.paddingR;
    n.paddingB = desc.paddingB;
    n.minSizeX = desc.minSizeX;
    n.minSizeY = desc.minSizeY;
    n.maxSizeX = desc.maxSizeX;
    n.maxSizeY = desc.maxSizeY;
    n.percentW = desc.percentW;
    n.percentH = desc.percentH;
    n.anchorX = desc.anchorX;
    n.anchorY = desc.anchorY;
    n.posX = desc.posX;
    n.posY = desc.posY;
    n.absolute = desc.absolute;
    n.textureId = desc.textureId;
    n.tintR = desc.tintR;
    n.tintG = desc.tintG;
    n.tintB = desc.tintB;
    n.tintA = desc.tintA;
    n.uv0x = desc.uv0x;
    n.uv0y = desc.uv0y;
    n.uv1x = desc.uv1x;
    n.uv1y = desc.uv1y;
    n.borderL = desc.borderL;
    n.borderT = desc.borderT;
    n.borderR = desc.borderR;
    n.borderB = desc.borderB;
    n.cornerRadius = desc.cornerRadius;
    n.wrapWidth = desc.wrapWidth;
    n.itemHeight = desc.itemHeight;
    n.flexDirection = desc.flexDirection;
    n.alignItems = desc.alignItems;
    n.justifyContent = desc.justifyContent;
    n.gap = desc.gap;
    n.flexGrow = desc.flexGrow;
    if (!desc.id.empty()) n.id = desc.id;

    if (desc.onClick) {
        if (n.handlerClick != 0) {
            tree.clickHandlers[size_t(n.handlerClick - 1)] = std::move(desc.onClick);
        } else {
            tree.clickHandlers.push_back(std::move(desc.onClick));
            n.handlerClick = static_cast<uint32_t>(tree.clickHandlers.size());
        }
    }
    if (desc.onToggle) {
        if (n.handlerToggle != 0) {
            tree.toggleHandlers[size_t(n.handlerToggle - 1)] = std::move(desc.onToggle);
        } else {
            tree.toggleHandlers.push_back(std::move(desc.onToggle));
            n.handlerToggle = static_cast<uint32_t>(tree.toggleHandlers.size());
        }
    }
    if (desc.onValue) {
        if (n.handlerValue != 0) {
            tree.valueHandlers[size_t(n.handlerValue - 1)] = std::move(desc.onValue);
        } else {
            tree.valueHandlers.push_back(std::move(desc.onValue));
            n.handlerValue = static_cast<uint32_t>(tree.valueHandlers.size());
        }
    }
    if (desc.onTextChange) {
        if (n.handlerText != 0) {
            tree.textHandlers[size_t(n.handlerText - 1)] = std::move(desc.onTextChange);
        } else {
            tree.textHandlers.push_back(std::move(desc.onTextChange));
            n.handlerText = static_cast<uint32_t>(tree.textHandlers.size());
        }
    }

    int child = n.firstChild;
    for (auto &ch : desc.children) {
        patchProps(tree, child, std::move(ch));
        child = tree.nodes[size_t(child)].nextSibling;
    }
}

}  // namespace

WidgetDesc window(std::string title, std::vector<WidgetDesc> children, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Window;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(title);
    d.children = std::move(children);
    return d;
}

WidgetDesc text(std::string content, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Text;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(content);
    return d;
}

WidgetDesc button(std::string label, std::string id, std::function<void()> onClick) {
    WidgetDesc d;
    d.type = NodeType::Button;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.onClick = std::move(onClick);
    return d;
}

WidgetDesc icon(Icon value, std::string id) { return text(iconGlyph(value), std::move(id)); }

WidgetDesc iconButton(Icon value, std::string label, std::string id,
                      std::function<void()> onClick) {
    return button(iconText(value, label), std::move(id), std::move(onClick));
}

WidgetDesc group(std::vector<WidgetDesc> children, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Group;
    d.id = std::move(id);
    d.key = d.id;
    d.children = std::move(children);
    return d;
}

WidgetDesc sameLine(std::string id) {
    WidgetDesc d;
    d.type = NodeType::SameLine;
    d.id = std::move(id);
    d.key = d.id;
    return d;
}

WidgetDesc separator(std::string id) {
    WidgetDesc d;
    d.type = NodeType::Separator;
    d.id = std::move(id);
    d.key = d.id;
    return d;
}

WidgetDesc checkbox(std::string label, bool checked, std::string id,
                    std::function<void(bool)> onToggle) {
    WidgetDesc d;
    d.type = NodeType::Checkbox;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.checked = checked;
    d.onToggle = std::move(onToggle);
    return d;
}

WidgetDesc slider(std::string label, float value, float minV, float maxV, std::string id,
                  std::function<void(float)> onValue) {
    WidgetDesc d;
    d.type = NodeType::Slider;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.value = value;
    d.minValue = minV;
    d.maxValue = maxV;
    d.onValue = std::move(onValue);
    return d;
}

WidgetDesc progress(float fraction, std::string id, std::string overlay) {
    WidgetDesc d;
    d.type = NodeType::Progress;
    d.id = std::move(id);
    d.key = d.id;
    d.value = fraction;
    d.text = std::move(overlay);
    return d;
}

WidgetDesc combo(std::string label, std::vector<std::string> options, int selected,
                 std::string id, std::function<void(float)> onValue) {
    WidgetDesc d;
    d.type = NodeType::Combo;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.value = float(std::max(0, selected));
    for (size_t i = 0; i < options.size(); ++i) {
        if (i) d.valueText += '\n';
        d.valueText += options[i];
    }
    d.onValue = std::move(onValue);
    return d;
}

WidgetDesc image(std::string id, float width, float height, std::function<void()> onClick) {
    WidgetDesc d;
    d.type = NodeType::Image;
    d.id = std::move(id);
    d.key = d.id;
    d.sizeX = width;
    d.sizeY = height;
    d.onClick = std::move(onClick);
    return d;
}

WidgetDesc imageButton(std::string id, float width, float height, std::function<void()> onClick) {
    WidgetDesc d;
    d.type = NodeType::ImageButton;
    d.id = std::move(id);
    d.key = d.id;
    d.sizeX = width;
    d.sizeY = height;
    d.onClick = std::move(onClick);
    return d;
}

WidgetDesc viewport(std::string id, float width, float height) {
    WidgetDesc d;
    d.type = NodeType::Viewport;
    d.id = std::move(id);
    d.key = d.id;
    d.sizeX = width;
    d.sizeY = height;
    return d;
}

WidgetDesc inputText(std::string label, std::string value, std::string id,
                     std::function<void(const std::string &)> onChange) {
    WidgetDesc d;
    d.type = NodeType::InputText;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.valueText = std::move(value);
    d.onTextChange = std::move(onChange);
    return d;
}

WidgetDesc searchField(std::string hint, std::string value, std::string id,
                       std::function<void(const std::string &)> onChange) {
    WidgetDesc d;
    d.type = NodeType::SearchField;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(hint);
    d.valueText = std::move(value);
    d.onTextChange = std::move(onChange);
    return d;
}

WidgetDesc toggleSwitch(std::string label, bool checked, std::string id,
                        std::function<void(bool)> onToggle) {
    WidgetDesc d;
    d.type = NodeType::Switch;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.checked = checked;
    d.onToggle = std::move(onToggle);
    return d;
}

WidgetDesc badge(std::string label, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Badge;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.tintR = 0.96f;
    d.tintG = 0.35f;
    d.tintB = 0.40f;
    d.tintA = 0.20f;
    return d;
}

WidgetDesc card(std::vector<WidgetDesc> children, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Card;
    d.id = std::move(id);
    d.key = d.id;
    d.children = std::move(children);
    return d;
}

WidgetDesc sectionHeader(std::string label, std::string id) {
    WidgetDesc d;
    d.type = NodeType::SectionHeader;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    return d;
}

WidgetDesc menuBar(std::vector<WidgetDesc> children, std::string id) {
    WidgetDesc d;
    d.type = NodeType::MenuBar;
    d.id = std::move(id);
    d.key = d.id;
    d.children = std::move(children);
    return d;
}

WidgetDesc menu(std::string label, std::vector<WidgetDesc> children, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Menu;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.children = std::move(children);
    return d;
}

WidgetDesc menuItem(std::string label, std::string shortcut, std::string id,
                    std::function<void()> onClick, bool selected) {
    WidgetDesc d;
    d.type = NodeType::MenuItem;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.valueText = std::move(shortcut);
    d.checked = selected;
    d.onClick = std::move(onClick);
    return d;
}

WidgetDesc collapsingHeader(std::string label, std::vector<WidgetDesc> children, std::string id,
                            bool defaultOpen) {
    WidgetDesc d;
    d.type = NodeType::CollapsingHeader;
    d.id = std::move(id);
    d.key = d.id;
    d.text = std::move(label);
    d.open = defaultOpen;
    d.children = std::move(children);
    return d;
}

WidgetDesc child(std::string id, std::vector<WidgetDesc> children, float width, float height) {
    WidgetDesc d;
    d.type = NodeType::Child;
    d.id = std::move(id);
    d.key = d.id;
    d.sizeX = width;
    d.sizeY = height;
    d.children = std::move(children);
    return d;
}

WidgetDesc scrollList(std::string id, std::vector<WidgetDesc> children, float height,
                      float itemHeight) {
    WidgetDesc d;
    d.type = NodeType::ScrollList;
    d.id = std::move(id);
    d.key = d.id;
    d.sizeY = height;
    d.itemHeight = itemHeight;
    d.children = std::move(children);
    return d;
}

WidgetDesc virtualList(std::string listId, const std::vector<std::string> &items, float height,
                       float itemHeight) {
    const std::string lid = listId;
    WidgetDesc inner = list(std::move(listId), items,
                            [lid](const std::string &label, int i) {
                                return button(label, lid + "/" + std::to_string(i))
                                    .withKey(lid + "/" + std::to_string(i));
                            });
    return scrollList(lid, std::move(inner.children), height, itemHeight);
}

WidgetDesc flex(FlexDirection direction, std::vector<WidgetDesc> children, std::string id) {
    WidgetDesc d;
    d.type = NodeType::Flex;
    d.id = std::move(id);
    d.key = d.id;
    d.flexDirection = direction;
    d.children = std::move(children);
    return d;
}

WidgetDesc row(std::vector<WidgetDesc> children, std::string id) {
    return flex(FlexDirection::Row, std::move(children), std::move(id));
}

WidgetDesc column(std::vector<WidgetDesc> children, std::string id) {
    return flex(FlexDirection::Column, std::move(children), std::move(id));
}

WidgetDesc spacer(std::string id, float grow) {
    WidgetDesc d;
    d.type = NodeType::Spacer;
    d.id = std::move(id);
    d.key = d.id;
    d.flexGrow = grow > 0.f ? grow : 1.f;
    return d;
}

WidgetDesc when(bool cond, WidgetDesc child) {
    if (!cond) return group({}, "__when_empty");
    return child;
}

WidgetDesc whenElse(bool cond, WidgetDesc ifTrue, WidgetDesc ifFalse) {
    return cond ? std::move(ifTrue) : std::move(ifFalse);
}

WidgetDesc list(std::string listId, const std::vector<std::string> &items,
                const std::function<WidgetDesc(const std::string &, int)> &itemFn) {
    std::vector<WidgetDesc> children;
    children.reserve(items.size());
    for (int i = 0; i < int(items.size()); ++i) {
        WidgetDesc row = itemFn(items[size_t(i)], i);
        if (row.id.empty()) row.id = listId + "/" + std::to_string(i);
        if (row.key.empty()) row.key = row.id;
        children.push_back(std::move(row));
    }
    return group(std::move(children), std::move(listId));
}

WidgetDesc listButtons(std::string listId, const std::vector<std::string> &items) {
    std::string lid = listId;
    return list(std::move(listId), items, [lid](const std::string &label, int i) {
        return button(label, lid + "/" + std::to_string(i)).withKey(lid + "/" + std::to_string(i));
    });
}

void applyTree(UIHost *host, WidgetDesc root) {
    if (!host) return;
    auto t = host->tree();
    t->nodes.clear();
    t->clickHandlers.clear();
    t->toggleHandlers.clear();
    t->valueHandlers.clear();
    t->textHandlers.clear();
    t->root = -1;
    t->root = appendNode(*t, std::move(root));
    t->dirty = true;
}

bool applyTreeReconcile(UIHost *host, WidgetDesc root) {
    if (!host) return true;
    auto t = host->tree();
    if (t->root < 0 || t->nodes.empty() || !structureMatches(*t, t->root, root)) {
        applyTree(host, std::move(root));
        return true;
    }
    patchProps(*t, t->root, std::move(root));
    t->dirty = false;
    return false;
}

}  // namespace eve::ui
