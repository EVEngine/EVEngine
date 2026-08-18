#include "scene/SceneComponent.h"

#include "scene/Scene.h"
#include "scene/TransformSystem.h"

namespace eve::scene {

void SceneComponent::attach(SceneHost *host) {
    if (host_ != host) {
        host_ = host;
        onMount(host);
    }
    dirty_ = true;
}

void SceneComponent::mountAs(const std::string &hostName) {
    SceneHost *h = Scene::create()->findHost(hostName);
    if (!h) h = SceneHost::createHost(hostName);
    attach(h);
    rebuild(true);
}

void SceneComponent::rebuild(bool forceFull) {
    if (!host_) return;
    NodeDesc tree = build();
    if (forceFull) host_->setTree(std::move(tree));
    else host_->setTreeReconcile(std::move(tree));
    TransformSystem::updateHost(host_);
    dirty_ = false;
}

bool SceneComponent::updateIfDirty() {
    if (!dirty_ || !host_) return false;
    rebuild(false);
    return true;
}

}  // namespace eve::scene
