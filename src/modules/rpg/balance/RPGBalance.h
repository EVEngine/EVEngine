#pragma once

#include "common/Export.h"
#include "common/Module.h"
#include "common/Value.h"

#include <memory>

namespace eve::rpg::balance {
/**
 * @brief Configured build/strategy/scenario tournament using production RPG Battle and settlement.
 * @ownership Owns validated suite definitions. Each measure owns a private ECS table and event scope.
 * Reports own values; no actor pointer, callback, event or handle escapes a bout.
 * @thread RPG owner thread, synchronous, configure/measure reentry forbidden. Registries/hooks must
 * not be changed during a tournament; skill/effect/class definitions are the game's canonical registries.
 * @remarks Version-1 suite uses instant turn-based skills; cast-time skills are explicitly rejected.
 */
class EVENGINE_API_BACKENDS RPGBalanceSuite {
public:
    /** @brief Create an unconfigured suite. */
    RPGBalanceSuite();
    /** @brief Release configuration; no game world state is owned between calls. */
    ~RPGBalanceSuite();
    RPGBalanceSuite(const RPGBalanceSuite&)            = delete;
    RPGBalanceSuite& operator=(const RPGBalanceSuite&) = delete;
    SSQ_REG;
    /** @brief Validate and atomically copy version-1 suite data; unknown fields/versions are rejected. */
    [[nodiscard]] Result<void> configure(const Value& suite);
    /**
     * @brief Project directed target graph edges into BalanceSearch metric targets.
     * @cost Linear in target edges; copy once when building a search specification.
     */
    [[nodiscard]] Result<Value> targets() const;
    /**
     * @brief Evaluate all unordered build pairs, independent strategy pairs, scenarios, seeds and both orders.
     * @param parameters Exact named candidate values; only isolated actor/gear projections are modified.
     * @param seeds Nonempty distinct uint64 seed list, encoded as integers or decimal strings;
     * Battle folds each seed into its 32-bit random stream.
     * @param dt Logical seconds per round for canonical skill cooldown and status advancement.
     * @param rounds Maximum rounds per bout; timeouts are reported as draws, never manufactured wins.
     * @return Versioned matrix, per-cell wins/draws/losses, Wilson score intervals, action evidence and metrics.
     * @cost Quadratic in builds and strategies, linear in scenarios, seeds and round limit; run offline.
     */
    [[nodiscard]] Result<Value> measure(const Value& parameters, const Value& seeds, double dt, int rounds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::rpg::balance
