#pragma once

#include "weapon/CombatCarrier.h"

#include <memory>
#include <optional>

namespace eve::weapon {
/**
 * @brief Optional gameplay-owned mapping from a physical contact to an ECS target.
 * @ownership The probe owns the mapper; implementations retain handles, never entity/body pointers.
 * @thread Simulation owner only, synchronously without locks. Do not mutate either world or the carrier pool.
 * @remarks Validate entity generations on each call. Return empty for scenery, failure for a stale required link.
 * Rebuild links after restore/hot reload; mappings are not persistent state.
 */
class IWorldHitTargetMapper {
public:
    /** @brief Release the mapping policy without destroying either world. */
    virtual ~IWorldHitTargetMapper() = default;
    /** @brief Resolve one borrowed contact for this call; return an owning optional target or diagnostic. */
    [[nodiscard]] virtual Result<std::optional<ecs::EntityHandle>> target(const WorldSphereContact& contact) const = 0;
};

/**
 * @brief Explicit sphere-query adapter; never registered or installed in a carrier runtime automatically.
 * @ownership Owns its query session and optional mapping policy, but neither simulation world.
 * @thread Simulation owner only; callbacks must not reenter or mutate the simulations.
 * @remarks A missing session reports Unsupported. World destruction/restore is detected by the session.
 * Destroying the probe first leaves physics intact. Recreate the session and gameplay links after restore.
 */
class EVENGINE_API_WORLD WorldHitProbe final : public ICarrierHitProbe {
public:
    /** @brief Take ownership of an optional query session; no entity mapping is installed. */
    explicit WorldHitProbe(std::unique_ptr<IWorldSphereSweep> sweep);
    /** @brief Take ownership of a query session and explicit gameplay mapping policy. */
    WorldHitProbe(std::unique_ptr<IWorldSphereSweep> sweep, std::unique_ptr<IWorldHitTargetMapper> mapper);
    WorldHitProbe(const WorldHitProbe&)            = delete;
    WorldHitProbe& operator=(const WorldHitProbe&) = delete;
    /**
     * @brief Project physical contacts without applying damage or forces.
     * @cost One sweep per proposed segment plus one mapper call per contact; amortized by simulation substeps.
     * @return Owning contacts with physical provenance; provider/mapping failure publishes no contacts.
     * Exceptions from the mapper become Failed diagnostics, so carrier updates retain frame atomicity.
     */
    [[nodiscard]] Result<std::vector<Contact>> query(CarrierHandle handle, const CarrierMotion& previous,
                                                     const CarrierMotion& current) const override;

private:
    std::unique_ptr<IWorldSphereSweep>     sweep_;
    std::unique_ptr<IWorldHitTargetMapper> mapper_;
};
}  // namespace eve::weapon
