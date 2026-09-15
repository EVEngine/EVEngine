#pragma once

/** @file ActionCollisionIgnoreState.h @brief Action adapter for temporary 3D body-pair collision ignores. */

#include "action/ActionStateWindowBlock.h"
#include "physics/PhysicsLink.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace eve::physics {
class World3D;
}

namespace eve::physics::action_adapter {

/** @brief Owning generation-qualified body pair selected for one collision channel. */
struct ActionCollisionPair {
    /** @brief First body link; ordering is canonicalized by the adapter. */
    PhysicsLink first;
    /** @brief Second distinct body link owned by the same World3D. */
    PhysicsLink second;
};

/**
 * @brief Resolves an action entity and semantic channel to two physics links.
 * @remarks Called synchronously on the owner thread without locks. It must not
 * throw, reenter the adapter, or retain the entity handle or channel view.
 */
using ActionCollisionPairResolver =
    std::function<Result<ActionCollisionPair>(ecs::EntityHandle, std::string_view channel)>;

/**
 * @brief Owns temporary body-pair collision overrides projected from Action windows.
 *
 * World3D remains the collision authority. This adapter stores only world/body
 * generation handles across dispatches, reference-counts overlapping windows,
 * and restores the pre-window pair state after the final exit. It observes the
 * world's weak lifetime token, so destroying the world first safely invalidates
 * all pending projections. Methods are owner-thread-only and callbacks are never
 * invoked while mutating World3D.
 */
class ActionCollisionIgnoreState final : public eve::action::IActionStateWindowSink {
public:
    /**
     * @brief Construct an adapter for one borrowed 3D world.
     * @param world World observed through its weak lifetime token; it may be destroyed first.
     * @param resolver Owning callback used only on window enter to resolve body links.
     */
    ActionCollisionIgnoreState(World3D& world, ActionCollisionPairResolver resolver);
    ~ActionCollisionIgnoreState() override;

    /** @brief Opt this adapter into or out of Action state-window dispatch. */
    void setEnabled(bool enabled);
    /** @brief Return whether this exact adapter is registered. */
    [[nodiscard]] bool enabled() const;
    /** @copydoc eve::action::IActionStateWindowSink::supports */
    [[nodiscard]] bool supports(eve::action::ActionStateWindowKind kind) const noexcept override;
    /** @copydoc eve::action::IActionStateWindowSink::enter */
    [[nodiscard]] Result<void> enter(const eve::action::ActionStateWindowBinding& binding,
                                     const eve::action::ActionTimelineEvent& event,
                                     const eve::action::ActionNotifyContext& context) override;
    /** @copydoc eve::action::IActionStateWindowSink::exit */
    [[nodiscard]] Result<void> exit(const eve::action::ActionStateWindowBinding& binding,
                                    const eve::action::ActionTimelineEvent& event,
                                    const eve::action::ActionNotifyContext& context) override;

    /**
     * @brief Unregister and restore every still-live pair to its pre-window state.
     * @return Applied when state was released, otherwise NoOp; stale bodies/worlds
     * are already cleared by the physics authority and are treated as released.
     */
    [[nodiscard]] Result<void> shutdown();
    /** @brief Number of active execution/item window keys. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_.size(); }
    /** @brief Number of distinct body pairs currently owned by the adapter. */
    [[nodiscard]] std::size_t pairCount() const noexcept { return pairs_.size(); }

private:
    using ActiveKey = std::pair<eve::action::ActionExecutionId, std::string>;
    struct PairKey {
        PhysicsBodyHandle first;
        PhysicsBodyHandle second;
        friend bool operator==(const PairKey&, const PairKey&) noexcept = default;
        friend auto operator<=>(const PairKey&, const PairKey&) noexcept = default;
    };
    struct PairState {
        bool        restoreEnabled = true;
        std::size_t owners = 0;
    };

    [[nodiscard]] Result<PairKey> resolvePair(ecs::EntityHandle subject, std::string_view channel) const;
    [[nodiscard]] World3D* liveWorld() const noexcept;
    void restorePair(const PairKey& key, const PairState& state) noexcept;
    void releaseAll() noexcept;

    World3D*                       world_ = nullptr;
    std::weak_ptr<const void>      worldLifetime_;
    PhysicsWorldHandle             worldHandle_ = PhysicsWorldHandle::invalid();
    ActionCollisionPairResolver    resolver_;
    std::map<ActiveKey, PairKey>   active_;
    std::map<PairKey, PairState>   pairs_;
};

}  // namespace eve::physics::action_adapter
