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
    node.visible = desc.visible;
    node.checked = desc.checked;
    node.open = desc.open;
    node.value = desc.value;
    node.minValue = desc.minValue;
    node.maxValue = desc.maxValue;
    node.sizeX = desc.sizeX;
    node.sizeY = desc.sizeY;
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
    n.visible = desc.visible;
    n.checked = desc.checked;
    n.open = desc.open;
    n.value = desc.value;
    n.minValue = desc.minValue;
    n.maxValue = desc.maxValue;
    n.sizeX = desc.sizeX;
    n.sizeY = desc.sizeY;
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
