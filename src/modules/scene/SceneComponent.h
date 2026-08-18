#pragma once

#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"

#include <string>

namespace eve::scene {

/**
 * @brief React-style scene component: implement build(), call setState / markDirty, then rebuild().
 * Mounts onto a named SceneHost via mountAs / attach.
 * Isomorphic to eve::ui::Component.
 */
class SceneComponent {
public:
    virtual ~SceneComponent() = default;

    virtual NodeDesc build() = 0;

    /** @brief Called once when the component is attached to a host. */
    virtual void onMount(SceneHost *host) {}

    void attach(SceneHost *host);
    void mountAs(const std::string &hostName);
    SceneHost *host() const { return host_; }

    /** @brief Rebuild tree onto host (reconcile by key when possible). */
    void rebuild(bool forceFull = false);

    void markDirty() { dirty_ = true; }
    bool isDirty() const { return dirty_; }

    /** @brief If dirty, rebuild and clear flag. Returns true if rebuilt. */
    bool updateIfDirty();

protected:
    /** @brief Subclasses call after mutating local state that affects build(). */
    void setState() { dirty_ = true; }

private:
    SceneHost *host_ = nullptr;
    bool dirty_ = true;
};

}  // namespace eve::scene
