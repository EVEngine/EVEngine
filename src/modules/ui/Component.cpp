#include "ui/Component.h"

#include "ui/UISystem.h"

namespace eve::ui {

void Component::attach(UIHostHandle host) {
    host_ = host;
    dirty_ = true;
}

void Component::mountAs(const std::string &hostName) {
    UIHostHandle h = UISystem::findHost(hostName);
    if (!UIHost::resolve(h)) h = UIHost::createHost(hostName);
    attach(h);
    rebuild(true);
}

void Component::rebuild(bool forceFull) {
    auto host = UIHost::resolve(host_);
    if (!host) return;
    WidgetDesc tree = build();
    if (forceFull) host->get().setTree(std::move(tree));
    else host->get().setTreeReconcile(std::move(tree));
    dirty_ = false;
}

bool Component::updateIfDirty() {
    if (!dirty_ || !UIHost::resolve(host_)) return false;
    rebuild(false);
    return true;
}

}  // namespace eve::ui
