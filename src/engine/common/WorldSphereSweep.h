#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include "common/Result.h"

namespace eve {
/** @brief Process-local physics identity projected losslessly from owner handles; never a save identity. */
struct WorldSweepIdentity {
    std::uint64_t world = 0;
    std::uint64_t body  = 0;
    /** @brief Compare both generation-qualified handles. */
    friend bool operator==(const WorldSweepIdentity&, const WorldSweepIdentity&) = default;
};
/** @brief Owning sphere sweep contact; position is the moving sphere center, not the obstacle surface. */
struct WorldSphereContact {
    WorldSweepIdentity identity;
    int                bodyId   = -1;
    int                shapeId  = -1;
    double             fraction = 0;
    double             x = 0, y = 0, z = 0;
    double             nx = 0, ny = 0, nz = 0;
    /** @brief Process-local world restore revision; zero means no revision-qualified provenance. */
    std::uint64_t worldEpoch = 0;
};
/** @brief Immutable per-binding sphere and filter policy. */
struct WorldSphereSweepSettings {
    float         radius        = 0.1f;
    std::uint32_t categoryBits  = 0xffffffffu;
    std::uint32_t maskBits      = 0xffffffffu;
    int           ignoredBodyId = -1;
};
/** @brief Optional physics-owned query session; retains only generation-qualified world/body handles.
 * @ownership Caller owns the session. World destruction or restore invalidates it; rebind after restore/hot reload.
 * @thread Owner simulation thread only. Calls invoke no scripts and hold no locks.
 */
class IWorldSphereSweep {
public:
    /** @brief Destroy a session without destroying its world. */
    virtual ~IWorldSphereSweep() = default;
    /** @brief Sweep new sphere crossings from start to end; zero-time initial overlaps are excluded.
     * Fail rather than truncate more than 256 shape contacts.
     * @cost One physics broad/narrow-phase query per proposed segment, amortized by simulation substeps.
     * @return Owning sorted contacts or a structured failure, including stale world/ignored-body identity.
     */
    [[nodiscard]] virtual Result<std::vector<WorldSphereContact>> query(double x, double y, double z, double endX,
                                                                        double endY, double endZ) const = 0;
};
/** @brief Optional binding capability; physics owns the factory for process lifetime. */
class IWorldSphereSweepFactory {
public:
    static constexpr const char* capabilityName = "eve.physics.IWorldSphereSweepFactory";
    /** @brief Release factory resources. */
    virtual ~IWorldSphereSweepFactory() = default;
    /** @brief Bind an opaque World3D query token and snapshot the ignored body's generation.
     * @ownership Returns an owning session; no raw world pointer survives this call.
     * @thread Owner simulation thread only; no callbacks.
     * @cost Allocates one query session per binding, amortized across all projectile updates.
     * @return Failure preserves any previous binding in the caller.
     */
    [[nodiscard]] virtual Result<std::unique_ptr<IWorldSphereSweep>> bind(std::string_view         worldToken,
                                                                          WorldSphereSweepSettings settings) const = 0;
};
}  // namespace eve
