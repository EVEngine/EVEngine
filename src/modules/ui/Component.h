#pragma once
#include "common/Export.h"


#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <memory>
#include <string>

namespace eve::ui {
namespace detail {
struct ComponentSchedule;
/** @brief Flush one batch of live dirty components before host traversal.
 * @thread Owner thread; newly queued edits wait for the next batch. Nested flushes wait for the next batch; no locks
 * around build callbacks.
 * @lifetime Weak registrations expire at component destruction; stale ECS hosts are skipped. */
EVENGINE_API_WORLD void flushPendingComponents();
}  // namespace detail

/**
 * @brief React-style UI component: implement build(); setState / markDirty schedules one update before UI rendering.
 * Mounts onto a named UIHost via mountAs / attach.
 */
class EVENGINE_API_WORLD Component {
public:
    /** @brief Create an unmounted component; attaching schedules its first update. */
    Component();
    Component(const Component &)            = delete;
    Component &operator=(const Component &) = delete;
    Component(Component &&)                 = delete;
    Component &operator=(Component &&)      = delete;
    /** @brief Component. */
    virtual ~Component();

    /** @brief Builds . */
    virtual WidgetDesc build() = 0;

    /** @brief Attaches . */
    void attach(UIHostHandle host);
    /** @brief Mounts as. */
    void mountAs(const std::string &hostName);
    /** @brief Returns the attached host handle, or an empty handle before attach. */
    [[nodiscard]] UIHostHandle host() const noexcept { return host_; }

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
    UIHostHandle host_{};
    bool dirty_ = true;
    bool                                       building_ = false;
    std::shared_ptr<detail::ComponentSchedule> schedule_;
};

}  // namespace eve::ui
