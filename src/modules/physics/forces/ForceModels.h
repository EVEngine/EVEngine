#pragma once

#include <limits>
#include "common/Result.h"

namespace eve::physics {
class Body;
class Body3D;

/** @brief Owning world-space SI vector; 2D uses XY with Z exactly zero. */
struct ForceVector {
    double x = 0, y = 0, z = 0;
};

/** @brief Spatial support; RectangleXY ignores Z, Box is axis-aligned in world space. */
enum class FieldRegionKind { Sphere, RectangleXY, Box, Global };

/** @brief SI field support; Sphere with negative radius retains the unbounded convention.
 * @details Boundary points have zero weight. Positive edgeWidth fades by the nearest interior face distance.
 * RectangleXY requires positive XY half extents and zero Z half extent; Box requires all positive.
 */
struct FieldRegion {
    ForceVector     center;
    double          radius    = -1;
    double          edgeWidth = 0;  ///< Linear interior fade in meters; cannot exceed the smallest support half size.
    FieldRegionKind kind      = FieldRegionKind::Sphere;
    ForceVector     halfExtents{1, 1, 1};
};

/** @brief Directional acceleration in m/s^2, independent of body mass. */
struct DirectionalField {
    FieldRegion region;
    ForceVector acceleration;
};

/** @brief Directional force in N, independent of the affected body's mass. */
struct DirectionalForceField {
    FieldRegion region;
    ForceVector force;
};

/** @brief Softened inverse-square field; positive strength attracts, negative repels. */
struct RadialField {
    FieldRegion region;
    double      strength   = 1;    ///< Gravitational parameter in m^3/s^2, not a relativistic black hole.
    double      softening  = 0.1;  ///< Strictly positive core radius in meters.
    double maxAcceleration = std::numeric_limits<double>::max();  ///< Finite nonnegative m/s^2 cap after region fade.
    double maxForce =
        std::numeric_limits<double>::max();  ///< Finite nonnegative N cap, applied together with acceleration cap.
};

/** @brief Displaced-volume buoyancy; caller supplies submerged volume and centroid. */
struct BuoyancyModel {
    ForceVector gravity{0, -9.81, 0};
    double      fluidDensity    = 1000;  ///< kg/m^3; a 2D slice must supply physical thickness.
    double      displacedVolume = 0;     ///< m^3 actually submerged, not total hull volume.
};

/** @brief Isotropic linear and quadratic drag against velocity relative to a medium. */
struct DampingModel {
    double linear    = 0;  ///< kg/s.
    double quadratic = 0;  ///< kg/m; aerodynamic value is rho * Cd * area / 2.
};

/** @brief Isotropic angular drag against body-minus-medium angular velocity.
 * @details Linear coefficient is kg*m^2/s; quadratic is kg*m^2 (radians dimensionless).
 * Distinct from translational DampingModel: inertia, not mass, controls angular deceleration.
 */
struct AngularDampingModel {
    double linear = 0, quadratic = 0;
};

/** @brief Wing geometry and game-oriented continuous stall polar, all in SI/radians. */
struct WingModel {
    ForceVector chord{1, 0, 0};   ///< Unit forward direction in world space.
    ForceVector normal{0, 1, 0};  ///< Unit upper surface normal, perpendicular to chord.
    double      density       = 1.225;
    double      area          = 1;
    double      liftSlope     = 6.283185307179586;  ///< Cl per radian before stall.
    double      zeroLiftAngle = 0;
    double      stallAngle    = 0.2617993877991494;
    double      baseDrag      = 0.02;
    double      aspectRatio   = 6;
    double      efficiency    = 0.8;
};

/** @brief Owning force and observable coefficients from a wing sample. */
struct WingSample {
    ForceVector force;
    double      angleOfAttack = 0, liftCoefficient = 0, dragCoefficient = 0;
};

/** @brief Coulomb static/kinetic friction coefficients (static >= kinetic >= 0). */
struct FrictionModel {
    double staticCoefficient  = 0.9;
    double kineticCoefficient = 0.7;
};

/** @brief Caller-owned wheel parameters; stiffness in N per unit slip / radian. */
struct TireModel {
    double radius = 0.3, inertia = 1;
    double friction = 1, longitudinalStiffness = 10000, corneringStiffness = 10000;
    double rollingResistance = 0.015;
    double minimumSlipSpeed  = 0.5;  ///< Positive low-speed regularization, m/s.
};

/** @brief Contact-frame sample: +X forward, +Y lateral; ground-relative m/s, N, Nm. */
struct TireInput {
    double longitudinalVelocity = 0, lateralVelocity = 0;
    double angularVelocity = 0;  ///< rad/s; positive rolls toward +X.
    double normalLoad = 0, driveTorque = 0, brakeTorque = 0;
    double longitudinalInverseMass = 0,
           lateralInverseMass      = 0;  ///< Effective contact inverse masses, 1/kg; zero prescribes chassis velocity.
};

/** @brief Owning wheel step result; publish angularVelocity once after a successful step. */
struct TireSample {
    double longitudinalForce = 0, lateralForce = 0;
    double slipRatio = 0, slipAngle = 0;
    double angularVelocity = 0, reactionTorque = 0;
    double resistanceTorque =
        0;  ///< Actual signed brake plus rolling-resistance torque, bounded by the requested capacity.
};

/**
 * @brief Pure force evaluators for 2D XY slices and full 3D, returning owning SI values.
 * @ownership Inputs are borrowed only for this call; no state or callbacks are retained.
 * @thread Any thread with immutable inputs; reentrant and allocation-free on success.
 * @return InvalidArgument for non-finite/invalid input or non-finite output; no partial mutation.
 * @details Time is injected in seconds. No RNG. Floating-point parity is tolerance-bounded
 * (relative 1e-5, absolute 1e-6), not bit-exact across platforms. Apply each sample once
 * before its fixed solver step. Damping is implicit over dt to avoid velocity reversal.
 * Tire contact uses implicit slip impulses with frozen per-step stiffness and a bounded brake solve.
 */
class ForceModels {
public:
    /** @brief Sample mass-scaled directional force at a world position in meters. */
    [[nodiscard]] static Result<ForceVector> directional(const DirectionalField& model, ForceVector position,
                                                         double mass);
    /** @brief Sample directional N force with region fade, without body-mass scaling. */
    [[nodiscard]] static Result<ForceVector> directionalForce(const DirectionalForceField& model, ForceVector position);
    /** @brief Sample attraction/repulsion; the softened center produces zero force. */
    [[nodiscard]] static Result<ForceVector> radial(const RadialField& model, ForceVector position, double mass);
    /** @brief Return -density * displacedVolume * gravity; apply at submerged centroid. */
    [[nodiscard]] static Result<ForceVector> buoyancy(const BuoyancyModel& model);
    /** @brief Implicit drag over positive dt; relativeVelocity is body minus medium. */
    [[nodiscard]] static Result<ForceVector> damping(const DampingModel& model, ForceVector relativeVelocity,
                                                     double mass, double dt);
    /** @brief Implicit planar angular drag; returns Nm for relative rad/s and positive kg*m^2 inertia.
     * @details Applied alone over dt, does not reverse relative rotation. Other torques remain independent.
     */
    [[nodiscard]] static Result<double> angularDamping2D(const AngularDampingModel& model,
                                                         double relativeAngularVelocity, double inertia, double dt);
    /** @brief Implicit angular drag in the body's principal-inertia frame; returns torque in that same frame.
     * @details All principal inertias must be positive kg*m^2. Rotate relative angular velocity into that frame,
     * then rotate returned Nm torque to world before applying. Does not include gyroscopic or external torques.
     */
    [[nodiscard]] static Result<ForceVector> angularDamping3D(const AngularDampingModel& model,
                                                              ForceVector                relativeAngularVelocity,
                                                              ForceVector principalInertia, double dt);
    /** @brief Wing force using body-minus-air velocity at the aerodynamic center. */
    [[nodiscard]] static Result<WingSample> wing(const WingModel& model, ForceVector relativeVelocity);
    /** @brief Cancel demanded tangent force at rest up to static limit; otherwise oppose slip. */
    [[nodiscard]] static Result<ForceVector> friction(const FrictionModel& model, ForceVector tangentVelocity,
                                                      ForceVector demandedForce, double normalLoad);
    /** @brief Combined-slip friction-circle tire with implicit wheel/chassis response and bounded brake locking.
     * @details Input inverse masses describe diagonal contact response; zero keeps the corresponding chassis velocity
     * prescribed. Returned forces and wheel speed share the same impulses. Slip diagnostics describe predicted
     * end-of-step relative motion. Uses a fixed bounded brake solve; InvalidArgument also reports
     * unrepresentable/nonconvergent numeric input.
     */
    [[nodiscard]] static Result<TireSample> tire(const TireModel& model, const TireInput& input, double dt);
};

/**
 * @brief Apply an SI force to a 2D body at an SI world point, converting through its world meter.
 * @ownership Body is borrowed synchronously, never retained; world owns its lifetime.
 * @thread Simulation owner thread, before step, outside callbacks; no reentry or callbacks.
 * @return InvalidArgument for non-planar/invalid values or inactive/non-dynamic body; stale body rejected.
 */
[[nodiscard]] Result<void> applyForceSI(Body& body, ForceVector force, ForceVector point);
/**
 * @brief Apply an SI force to a 3D body at an SI world point.
 * @ownership Body is borrowed synchronously, never retained; world owns its lifetime.
 * @thread Simulation owner thread, before step, outside callbacks; no reentry or callbacks.
 * @return InvalidArgument for invalid values or inactive/non-dynamic body; stale body rejected.
 */
[[nodiscard]] Result<void> applyForceSI(Body3D& body, ForceVector force, ForceVector point);
}  // namespace eve::physics
