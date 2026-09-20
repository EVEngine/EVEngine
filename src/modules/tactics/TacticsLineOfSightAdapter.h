#pragma once

/**
 * @file TacticsLineOfSightAdapter.h
 * @brief Grid line of sight for `sensing` consumers, answered from a bound tactics board.
 *
 * The gap analysis flagged that `sensing::TargetingResolver`'s `LineOfSightMode::Required` had no
 * grid implementation at all: only a world-space physics probe existed, so a turn-based game could
 * not ask the targeting pipeline "can this unit see that cell". This adapter supplies it from the
 * board that actually owns a sight-blocking fact - tactics cells carry the persisted
 * `sight_blocker` tag - instead of inventing a second blocking model somewhere else.
 *
 * Shape, mirroring the physics adapter:
 *  - the adapter is registered once for the `Grid2D`/`Grid3D` spaces through
 *    `sensing::LineOfSightRouter`, so it does not displace the world-space provider;
 *  - it answers for **one bound board at a time**. Binding a second, different battle is a
 *    Conflict: a project must say which board the pipeline should see rather than getting an
 *    answer that depends on binding order;
 *  - the bound battle is held as an **ECS handle, not a pointer**: a battle that was released
 *    resolves to nothing, so a query reports `Unsupported` instead of dereferencing freed state;
 *  - a query before anything is bound is `Unsupported`, never "not visible".
 */

#include "common/ECS.h"
#include "common/Result.h"
#include "sensing/Targeting.h"

namespace eve::tactics {

/**
 * @brief Answers grid line-of-sight queries from the bound tactics battle.
 *
 * @thread Affine to the simulation thread; it performs no synchronization and ECS resolution is
 *         owner-thread-affine.
 * @reentrancy `query` reads the board and its sight policy synchronously and invokes no callbacks.
 */
class TacticsLineOfSightAdapter final : public sensing::ILineOfSightQuery {
public:
    /**
     * @brief Bind the battle this adapter answers for.
     * @param battle Handle of a live module-owned battle.
     * @return Applied, NoOp when it is already the bound battle, Conflict when a different battle
     *         is bound, or StaleHandle when the handle does not resolve.
     */
    [[nodiscard]] Result<void> bindBattle(ecs::EntityHandle battle);

    /** @brief Release the binding when @p battle is the bound one; otherwise NoOp. */
    [[nodiscard]] Result<void> unbindBattle(ecs::EntityHandle battle);

    /** @brief The currently bound battle, or an invalid handle when none is bound. */
    [[nodiscard]] ecs::EntityHandle boundBattle() const noexcept { return battle_; }

    /** @brief Whether a battle is currently bound. */
    [[nodiscard]] bool hasBoundBattle() const noexcept { return bound_; }

    /** @copydoc sensing::ILineOfSightQuery::query */
    [[nodiscard]] Result<sensing::LineOfSightResult> query(const sensing::TargetLocation& from,
                                                           const sensing::TargetLocation& to) const override;

private:
    ecs::EntityHandle battle_{};
    /**
     * @brief Whether @ref battle_ is bound.
     *
     * Tracked explicitly because `ecs::EntityHandle` has no equality or null comparison; the
     * adapter compares handles field by field instead of relying on a sentinel.
     */
    bool              bound_ = false;
};

/** @brief The process-wide adapter instance the capability answers with. */
[[nodiscard]] TacticsLineOfSightAdapter& tacticsLineOfSightAdapter();

/**
 * @brief Claim the grid coordinate spaces for the tactics adapter.
 *
 * @return Applied when this call claimed the spaces, NoOp when they were already claimed by this
 *         adapter, or the router's own refusal (Conflict for a foreign provider, Unsupported when
 *         no router is available).
 * @remarks Called from the module constructor, so importing `tactics` is what makes grid line of
 *          sight available to the targeting pipeline.
 */
[[nodiscard]] Result<void> registerTacticsLineOfSightProvider();

}  // namespace eve::tactics
