#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <algorithm>

namespace eve::ui {
namespace {

uint32_t g_anonHostSeq = 0;

bool focusableType(NodeType type) {
    switch (type) {
        case NodeType::Button:
        case NodeType::Checkbox:
        case NodeType::Slider:
        case NodeType::InputText:
        case NodeType::ImageButton:
        case NodeType::Combo:
        case NodeType::Viewport:
        case NodeType::SearchField:
        case NodeType::Switch:
        case NodeType::MenuItem: return true;
        default: return false;
    }
}

bool eligible(const UINode &node, bool sequential) {
    return focusableType(node.type) && !node.id.empty() && node.visible && node.enabled &&
           (sequential ? node.focusMode == FocusMode::All
                       : node.focusMode != FocusMode::None);
}

const std::string &neighbor(const UINode &node, FocusDirection direction) {
    switch (direction) {
        case FocusDirection::Next: return node.focusNext;
        case FocusDirection::Previous: return node.focusPrevious;
        case FocusDirection::Left: return node.focusLeft;
        case FocusDirection::Right: return node.focusRight;
        case FocusDirection::Up: return node.focusUp;
        case FocusDirection::Down: return node.focusDown;
    }
    return node.focusNext;
}

}  // namespace

UIHostHandle UIHost::createHost(const std::string &name) {
    UIHost *h = UIHost::create();
    if (!h) return {};
    const UIHostHandle handle = ecs::handle_of(h);
    h->meta()->entity         = handle;
    if (name.empty()) {
        h->meta()->name = "host" + std::to_string(++g_anonHostSeq);
    } else {
        h->meta()->name = name;
    }
    return handle;
}

eve::OptionalRef<UIHost> UIHost::resolve(UIHostHandle handle) noexcept {
    auto *entity = ecs::try_get(handle);
    if (auto *host = entity ? dynamic_cast<UIHost *>(entity) : nullptr) return std::ref(*host);
    return std::nullopt;
}

void UIHost::setName(const std::string &name) { meta()->name = name; }

const std::string &UIHost::getName() { return meta()->name; }

void UIHost::setWorldAnchor(float x, float y, float z) {
    auto anchor = worldAnchor();
    anchor->enabled = true;
    anchor->worldX = x;
    anchor->worldY = y;
    anchor->worldZ = z;
}

void UIHost::clearWorldAnchor() {
    auto anchor = worldAnchor();
    anchor->enabled = false;
    anchor->state = WorldAnchorState::Disabled;
    anchor->scale = 1.f;
}

void UIHost::setTree(WidgetDesc root) { applyTree(this, std::move(root)); }

bool UIHost::setTreeReconcile(WidgetDesc root) { return applyTreeReconcile(this, std::move(root)); }

void UIHost::setSimplePanel(const std::string &title, const std::string &labelText,
                            const std::string &buttonText, const std::string &labelId,
                            const std::string &buttonId) {
    setTree(window(title,
                   {
                       text(labelText, labelId),
                       button(buttonText, buttonId),
                   },
                   "root"));
}

eve::OptionalRef<UINode> UIHost::findById(const std::string &id) {
    if (id.empty()) return std::nullopt;
    auto t = tree();
    for (auto &n : t->nodes) {
        if (n.id == id) return std::ref(n);
    }
    return std::nullopt;
}

eve::OptionalRef<UINode> UIHost::findByKey(const std::string &key) {
    if (key.empty()) return std::nullopt;
    auto t = tree();
    for (auto &n : t->nodes) {
        if (n.key == key) return std::ref(n);
    }
    return std::nullopt;
}

void UIHost::setTextById(const std::string &id, const std::string &text) {
    if (auto n = findById(id)) n->get().text = text;
}

void UIHost::setVisibleById(const std::string &id, bool visible) {
    if (auto n = findById(id)) n->get().visible = visible;
}

void UIHost::setEnabledById(const std::string &id, bool enabled) {
    if (auto n = findById(id)) {
        n->get().enabled = enabled;
        if (!enabled) {
            n->get().focusRequested = false;
            n->get().focused        = false;
        }
    }
}

void UIHost::setCheckedById(const std::string &id, bool checked) {
    if (auto n = findById(id)) n->get().checked = checked;
}

void UIHost::setValueById(const std::string &id, float value) {
    if (auto n = findById(id)) n->get().value = value;
}

void UIHost::setValueTextById(const std::string &id, const std::string &value) {
    if (auto n = findById(id)) n->get().valueText = value;
}

bool UIHost::requestFocusById(const std::string &id) {
    auto target = findById(id);
    if (!target || !eligible(target->get(), false)) return false;
    auto t = tree();
    for (auto &node : t->nodes) node.focusRequested = false;
    target->get().focusRequested = true;
    return true;
}

bool UIHost::moveFocus(FocusDirection direction, bool wrap) {
    auto t = tree();

    UINode *current = nullptr;
    for (UINode &node : t->nodes) {
        if (node.focused) {
            current = &node;
            break;
        }
        if (!current && node.focusRequested) current = &node;
    }

    if (current) {
        const std::string &explicitId = neighbor(*current, direction);
        if (!explicitId.empty()) {
            auto explicitTarget = findById(explicitId);
            if (explicitTarget && eligible(explicitTarget->get(), true)) return requestFocusById(explicitId);
        }
    }

    std::vector<std::pair<int, std::size_t>> order;
    order.reserve(t->nodes.size());
    for (std::size_t index = 0; index < t->nodes.size(); ++index) {
        const UINode &node = t->nodes[index];
        if (eligible(node, true) && node.tabIndex >= 0)
            order.emplace_back(node.tabIndex, index);
    }
    if (order.empty()) return false;
    std::stable_sort(order.begin(), order.end());

    const bool backwards = direction == FocusDirection::Previous ||
                           direction == FocusDirection::Left ||
                           direction == FocusDirection::Up;
    std::size_t targetOrder = backwards ? order.size() - 1 : 0;
    if (current) {
        const auto found = std::find_if(order.begin(), order.end(),
                                        [current, &t](const auto &entry) {
                                            return &t->nodes[entry.second] == current;
                                        });
        if (found != order.end()) {
            const std::size_t position = static_cast<std::size_t>(found - order.begin());
            if (backwards) {
                if (position == 0 && !wrap) return false;
                targetOrder = position == 0 ? order.size() - 1 : position - 1;
            } else {
                if (position + 1 == order.size() && !wrap) return false;
                targetOrder = position + 1 == order.size() ? 0 : position + 1;
            }
        }
    }
    return requestFocusById(t->nodes[order[targetOrder].second].id);
}

std::string UIHost::focusedId() {
    auto t = tree();
    for (const auto &node : t->nodes) {
        if (node.focused) return node.id;
    }
    return {};
}

bool UIHost::setClickHandler(const std::string &id, std::function<void()> fn) {
    auto n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->get().handlerClick != 0) {
        size_t idx = size_t(n->get().handlerClick - 1);
        if (idx < t->clickHandlers.size()) {
            t->clickHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->clickHandlers.push_back(std::move(fn));
    n->get().handlerClick = static_cast<uint32_t>(t->clickHandlers.size());
    return true;
}

bool UIHost::setToggleHandler(const std::string &id, std::function<void(bool)> fn) {
    auto n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->get().handlerToggle != 0) {
        size_t idx = size_t(n->get().handlerToggle - 1);
        if (idx < t->toggleHandlers.size()) {
            t->toggleHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->toggleHandlers.push_back(std::move(fn));
    n->get().handlerToggle = static_cast<uint32_t>(t->toggleHandlers.size());
    return true;
}

bool UIHost::setValueHandler(const std::string &id, std::function<void(float)> fn) {
    auto n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->get().handlerValue != 0) {
        size_t idx = size_t(n->get().handlerValue - 1);
        if (idx < t->valueHandlers.size()) {
            t->valueHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->valueHandlers.push_back(std::move(fn));
    n->get().handlerValue = static_cast<uint32_t>(t->valueHandlers.size());
    return true;
}

bool UIHost::setTextHandler(const std::string &id, std::function<void(const std::string &)> fn) {
    auto n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->get().handlerText != 0) {
        size_t idx = size_t(n->get().handlerText - 1);
        if (idx < t->textHandlers.size()) {
            t->textHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->textHandlers.push_back(std::move(fn));
    n->get().handlerText = static_cast<uint32_t>(t->textHandlers.size());
    return true;
}

}  // namespace eve::ui
