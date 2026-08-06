#include "ui/Component.h"

#include "ui/UISystem.h"

namespace eve::ui {

void Component::attach(UIHost *host) {
    host_ = host;
    dirty_ = true;
}

void Component::mountAs(const std::string &hostName) {
    UIHost *h = UISystem::findHost(hostName);
    if (!h) h = UIHost::createHost(hostName);
    attach(h);
    rebuild(true);
}

void Component::rebuild(bool forceFull) {
    if (!host_) return;
    WidgetDesc tree = build();
    if (forceFull) host_->setTree(std::move(tree));
    else host_->setTreeReconcile(std::move(tree));
    dirty_ = false;
}

bool Component::updateIfDirty() {
    if (!dirty_ || !host_) return false;
    rebuild(false);
    return true;
}

}  // namespace eve::ui
