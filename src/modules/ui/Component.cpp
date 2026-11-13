// eve-hot-path
#include "ui/Component.h"
#include <vector>

#include "ui/UISystem.h"

namespace eve::ui {
namespace detail {
struct ComponentSchedule {
    Component *component = nullptr;  // Owner invalidates before destruction; never exposed as a handle.
    bool       queued    = false;
    std::optional<std::string> windowTitle;
};
namespace {
std::vector<std::weak_ptr<ComponentSchedule>> pending;
std::vector<std::weak_ptr<ComponentSchedule>> draining;
bool                                          flushing = false;
}  // namespace
void flushPendingComponents() {
    if (flushing) return;
    flushing = true;
    struct FlushGuard {
        ~FlushGuard() {
            draining.clear();
            flushing = false;
        }
    } guard;
    draining.swap(pending);
    for (size_t i = 0; i < draining.size(); ++i) {
        auto &entry        = draining[i];
        auto  registration = entry.lock();
        if (!registration || !registration->component) continue;
        registration->queued = false;
        try {
            registration->component->updateIfDirty();
        } catch (...) {
            pending.insert(pending.end(), draining.begin() + i + 1, draining.end());
            throw;
        }
    }
}
}  // namespace detail

Component::Component() : schedule_(std::make_shared<detail::ComponentSchedule>()) { schedule_->component = this; }
Component::~Component() { schedule_->component = nullptr; }
void Component::markDirty() {
    dirty_ = true;
    if (schedule_->queued) return;
    detail::pending.push_back(schedule_);
    schedule_->queued = true;
}


void Component::setWindowFrame(std::optional<std::string> title) {
    markDirty();
    schedule_->windowTitle = std::move(title);
}

void Component::attach(UIHostHandle host) {
    host_ = host;
    markDirty();
}

void Component::mountAs(const std::string &hostName) {
    UIHostHandle h = UISystem::findHost(hostName);
    if (!UIHost::resolve(h)) h = UIHost::createHost(hostName);
    attach(h);
    rebuild(true);
}

void Component::rebuild(bool forceFull) {
    auto host = UIHost::resolve(host_);
    if (!host || building_) return;
    building_ = true;
    dirty_    = false;
    try {
        const auto title = schedule_->windowTitle;
        auto tree = build();
        if (title && tree.type != NodeType::Window) tree = window(*title, {std::move(tree)});
        host      = UIHost::resolve(host_);
        if (host) {
            if (forceFull)
                host->get().setTree(std::move(tree));
            else
                host->get().setTreeReconcile(std::move(tree));
        }
    } catch (...) {
        building_ = false;
        markDirty();
        throw;
    }
    building_ = false;
}

bool Component::updateIfDirty() {
    if (building_ || !dirty_ || !UIHost::resolve(host_)) return false;
    rebuild(false);
    return true;
}

}  // namespace eve::ui
