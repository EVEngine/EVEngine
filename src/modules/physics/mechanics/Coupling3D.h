#pragma once
#include <memory>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::physics {
class World3D;
class Body3D;
class Joint3D;

/** @brief Owning SI point (meters); pulley attachments use body-local points, guides use world points. */
struct CouplingPoint3D {
    float x = 0, y = 0, z = 0;
};
/** @brief Stabilization and force limits for an ideal mechanical transmission. */
struct CouplingLimits3D {
    float positionCorrection = 0.2f;       ///< Fraction of position error corrected per step, in [0,1].
    float maxCorrectionSpeed = 10.f;       ///< Generalized m/s or rad/s, according to the first coordinate.
    float maxForce           = 1000000.f;  ///< Generalized N or Nm; finite and positive.
};
/** @brief Gear relation q1 + ratio*q2 = constant captured at creation. Mixed joints give ratio length-dependent units.
 */
struct GearCoupling3DSettings {
    float            ratio = 1.f;
    CouplingLimits3D limits;
};
/** @brief Weighted rope length lA + ratio*lB; ideal taut pulley by default, optional tension-only slack rope. */
struct PulleyCoupling3DSettings {
    CouplingPoint3D  localAnchorA, localAnchorB;
    CouplingPoint3D  groundA{-1, 2, 0}, groundB{1, 2, 0};
    float            ratio = 1.f, length = 4.f;
    bool             tensionOnly = false;
    CouplingLimits3D limits;
};
/** @brief Owning observation; coordinates/error are m or rad, impulse is the last completed generalized impulse. */
struct Coupling3DState {
    double coordinateA = 0, coordinateB = 0, error = 0, impulse = 0;
    bool   active = false, saturated = false;
};
/**
 * @brief Owning 3D transmission constraint, stepped transactionally by its World3D.
 * @ownership Caller/VM owns wrapper; World registers an observer and invalidates it before teardown.
 * @lifetime Body/source-joint/world destruction invalidates the wrapper; no backend pointers persist across steps.
 * @thread All operations and destruction require the simulation owner thread, outside callbacks/World3D::step.
 * @reentrancy No callbacks or locks. Source references are borrowed only for construction.
 * @details Explicit velocity projection precedes the native solver; positional drift uses bounded bias correction.
 * Revolute coordinates unwrap across turns and require less than pi radians of relative rotation per step.
 * This is a finite-step transmission model; it does not simulate tooth contact or promise bit-exact replay.
 */
class EVENGINE_API_WORLD Coupling3D {
public:
    /** @brief Releases the registration; source bodies and joints remain intact. */
    ~Coupling3D();
    Coupling3D(const Coupling3D&)            = delete;
    Coupling3D& operator=(const Coupling3D&) = delete;
    /** @brief Couple two live same-world revolute/prismatic joints with finite nonzero signed ratio.
     * @return Owning transmission or structured invalid/stale/locked/degenerate input failure; failure does not mutate
     * bodies.
     * @cost Endpoint lookup scales with world body count, plus wrapper allocation; amortize over simulation steps.
     */
    [[nodiscard]] static Result<std::unique_ptr<Coupling3D>> createGear(World3D& world, Joint3D& first, Joint3D& second,
                                                                        const GearCoupling3DSettings& settings);
    /** @brief Connect distinct same-world bodies to fixed guides with positive lengths and ratio.
     * @return Owning transmission or structured failure; at least one movable degree of freedom is required.
     * @cost Endpoint lookup scales with world body count, plus wrapper allocation; amortize over simulation steps.
     */
    [[nodiscard]] static Result<std::unique_ptr<Coupling3D>> createPulley(World3D& world, Body3D& a, Body3D& b,
                                                                          const PulleyCoupling3DSettings& settings);
    /** @brief True while the world and all transmission sources exist. */
    [[nodiscard]] bool isValid() const noexcept;
    /** @brief Stable world-local transmission id, retained after invalidation. */
    [[nodiscard]] int getId() const noexcept;
    /** @brief Weak identity token for owner-thread observers; expires on release, source/world destruction or restore.
     * @ownership Does not own the transmission. Locking the token does not extend solver or world lifetime.
     * @thread Simulation owner only; no callbacks or reentrancy.
     */
    [[nodiscard]] std::weak_ptr<const void> lifetimeToken() const noexcept { return lifetime_; }
    /** @brief Enable/disable force projection while retaining phase tracking; locked/dead objects reject changes. */
    [[nodiscard]] Result<void> setEnabled(bool enabled);
    /** @brief Read current coordinates and last completed impulse without advancing phase tracking. */
    [[nodiscard]] Result<Coupling3DState> sample() const;
    /** @brief Remove the transmission; repeated destruction succeeds, a stepping world rejects mutation. */
    [[nodiscard]] Result<void> destroy();

private:
    friend class World3D;
    friend struct WorldSnapshotAccess;
    struct Data;
    explicit Coupling3D(std::unique_ptr<Data> data);
    void                        release() noexcept;
    std::unique_ptr<Data>       data_;
    std::shared_ptr<const void> lifetime_ = std::make_shared<int>(0);
};
}  // namespace eve::physics
