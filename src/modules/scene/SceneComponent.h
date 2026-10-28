#pragma once

#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"

#include <memory>
#include <string>

namespace eve::scene {
namespace detail {
struct ComponentSchedule;
/** @brief Flush one batch of live dirty components before host traversal.
 * @thread Owner thread; newly queued edits wait for the next batch. Nested flushes wait for the next batch; no locks
 * around build callbacks.
 * @lifetime Weak registrations expire at component destruction; stale ECS hosts are skipped. */
EVENGINE_API_PLATFORM void flushPendingComponents();
}  // namespace detail

/**
 * @brief React-style scene component: implement build(); setState / markDirty schedules one update before transform
 * traversal. Mounts onto a named SceneHost via mountAs / attach. Isomorphic to eve::ui::Component.
 */
class EVENGINE_API_PLATFORM SceneComponent {
public:
    /** @brief Create an unmounted component; attaching schedules its first update. */
    SceneComponent();
    SceneComponent(const SceneComponent &)            = delete;
    SceneComponent &operator=(const SceneComponent &) = delete;
    SceneComponent(SceneComponent &&)                 = delete;
    SceneComponent &operator=(SceneComponent &&)      = delete;
    /** @brief Scene component. */
    virtual ~SceneComponent();

    /** @brief Builds . */
    virtual NodeDesc build() = 0;

    /** @brief Called once when the component is attached to a host. */
    virtual void onMount(SceneHost *host) {}

    /** @brief Attaches . */
    void attach(SceneHost *host);
    /** @brief Mounts as. */
    void mountAs(const std::string &hostName);
    /**
     * @brief Returns the currently attached host, or null before attach.
     * @return Borrowed nullable SceneHost owned by the scene ECS/world registry.
     * @ownership SceneComponent does not own the host and must not delete it.
     * @lifetime Valid until detach/host destruction; do not retain across rebuild or module teardown.
     * @thread Call on the scene thread that owns the host.
     * @reentrancy This accessor invokes no callbacks and is not a synchronization primitive.
     */
    SceneHost *host() const;

    /** @brief Compatibility-only synchronous refresh; normal edits use markDirty / setState.
     * @cost Builds the complete description tree and reconciles by key; call once per edit batch.
     * @reentrancy May invoke build(); recursively rebuilding or destroying this component during build is invalid. */
    void rebuild(bool forceFull = false);

    /** @brief Schedule one rebuild; repeated calls before the next preparation phase coalesce.
     * @thread Owner thread; no callbacks. Edits during build schedule the following batch.
     * @lifetime Destroying the component invalidates its weak scheduling registration. */
    void markDirty();
    /** @brief True when dirty. */
    bool isDirty() const { return dirty_; }

    /** @brief Compatibility-only synchronous update; normal edits are flushed automatically.
     * @return True when a live dirty component was rebuilt.
     * @cost Complete tree construction/reconciliation when dirty; amortize once per edit batch. */
    bool updateIfDirty();

protected:
    /** @brief Subclasses call after mutating local state that affects build(). */
    void setState() { markDirty(); }

private:
    ecs::EntityHandle                          hostHandle_{};
    bool dirty_ = true;
    bool                                       building_ = false;
    std::shared_ptr<detail::ComponentSchedule> schedule_;
};

}  // namespace eve::scene
