#include "ui/UIHost.h"
#include "ui/Widget.h"

namespace eve::ui {
namespace {

uint32_t g_anonHostSeq = 0;

}  // namespace

UIHost *UIHost::createHost(const std::string &name) {
    UIHost *h = UIHost::create();
    h->meta()->entity = h;
    if (name.empty()) {
        h->meta()->name = "host" + std::to_string(++g_anonHostSeq);
    } else {
        h->meta()->name = name;
    }
    return h;
}

void UIHost::setName(const std::string &name) { meta()->name = name; }

const std::string &UIHost::getName() { return meta()->name; }

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

UINode *UIHost::findById(const std::string &id) {
    if (id.empty()) return nullptr;
    auto t = tree();
    for (auto &n : t->nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

UINode *UIHost::findByKey(const std::string &key) {
    if (key.empty()) return nullptr;
    auto t = tree();
    for (auto &n : t->nodes) {
        if (n.key == key) return &n;
    }
    return nullptr;
}

void UIHost::setTextById(const std::string &id, const std::string &text) {
    if (auto *n = findById(id)) n->text = text;
}

void UIHost::setVisibleById(const std::string &id, bool visible) {
    if (auto *n = findById(id)) n->visible = visible;
}

void UIHost::setCheckedById(const std::string &id, bool checked) {
    if (auto *n = findById(id)) n->checked = checked;
}

void UIHost::setValueById(const std::string &id, float value) {
    if (auto *n = findById(id)) n->value = value;
}

void UIHost::setValueTextById(const std::string &id, const std::string &value) {
    if (auto *n = findById(id)) n->valueText = value;
}

bool UIHost::setClickHandler(const std::string &id, std::function<void()> fn) {
    auto *n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->handlerClick != 0) {
        size_t idx = size_t(n->handlerClick - 1);
        if (idx < t->clickHandlers.size()) {
            t->clickHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->clickHandlers.push_back(std::move(fn));
    n->handlerClick = static_cast<uint32_t>(t->clickHandlers.size());
    return true;
}

bool UIHost::setToggleHandler(const std::string &id, std::function<void(bool)> fn) {
    auto *n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->handlerToggle != 0) {
        size_t idx = size_t(n->handlerToggle - 1);
        if (idx < t->toggleHandlers.size()) {
            t->toggleHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->toggleHandlers.push_back(std::move(fn));
    n->handlerToggle = static_cast<uint32_t>(t->toggleHandlers.size());
    return true;
}

bool UIHost::setValueHandler(const std::string &id, std::function<void(float)> fn) {
    auto *n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->handlerValue != 0) {
        size_t idx = size_t(n->handlerValue - 1);
        if (idx < t->valueHandlers.size()) {
            t->valueHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->valueHandlers.push_back(std::move(fn));
    n->handlerValue = static_cast<uint32_t>(t->valueHandlers.size());
    return true;
}

bool UIHost::setTextHandler(const std::string &id, std::function<void(const std::string &)> fn) {
    auto *n = findById(id);
    if (!n) return false;
    auto t = tree();
    if (n->handlerText != 0) {
        size_t idx = size_t(n->handlerText - 1);
        if (idx < t->textHandlers.size()) {
            t->textHandlers[idx] = std::move(fn);
            return true;
        }
    }
    t->textHandlers.push_back(std::move(fn));
    n->handlerText = static_cast<uint32_t>(t->textHandlers.size());
    return true;
}

}  // namespace eve::ui
