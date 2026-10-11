#include "scene/SceneComponent.h"
#include <vector>

#include "scene/Scene.h"
#include "scene/TransformSystem.h"

#include <utility>

namespace eve::scene {
namespace detail {
struct ComponentSchedule {
    SceneComponent *component = nullptr;  // Owner invalidates before destruction; never exposed as a handle.
    bool            queued    = false;
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

SceneComponent::SceneComponent() : schedule_(std::make_shared<detail::ComponentSchedule>()) {
    schedule_->component = this;
}
SceneComponent::~SceneComponent() { schedule_->component = nullptr; }
void SceneComponent::markDirty() {
    dirty_ = true;
    if (schedule_->queued) return;
    detail::pending.push_back(schedule_);
    schedule_->queued = true;
}


SceneHost *SceneComponent::host() const { return dynamic_cast<SceneHost *>(ecs::try_get(hostHandle_)); }

void SceneComponent::attach(SceneHost *nextHost) {
    if (host() != nextHost) {
        hostHandle_ = ecs::handle_of(nextHost);
        onMount(nextHost);
    }
    markDirty();
}

void SceneComponent::mountAs(const std::string &hostName) {
    SceneHost *h     = nullptr;
    auto       found = Scene::create()->findHost(hostName);
    if (found.ok()) {
        h = std::move(found).takeValue();
    } else {
        auto created = SceneHost::createHost(hostName);
        if (!created.ok()) return;
        h = std::move(created).takeValue();
    }
    attach(h);
    rebuild(true);
}

void SceneComponent::rebuild(bool forceFull) {
    if (!host() || building_) return;
    building_ = true;
    dirty_ = false;
    try {
        auto  tree   = build();
        auto *target = host();
        if (target) {
            if (forceFull)
                target->setTree(std::move(tree));
            else
                target->setTreeReconcile(std::move(tree));
            TransformSystem::updateHost(target);
        }
    } catch (...) {
        building_ = false;
        markDirty();
        throw;
    }
    building_ = false;
}

bool SceneComponent::updateIfDirty() {
    if (building_ || !dirty_ || !host()) return false;
    rebuild(false);
    return true;
}

}  // namespace eve::scene
