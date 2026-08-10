#pragma once

#include <cmath>
#include <algorithm>

namespace eve::animation {

/**
 * Control-theory helpers for procedural animation.
 *
 * Sources (public literature / industry practice):
 * - Second-order LTI tracking (t3ssel8r): ÿ + k1 ẏ + k2 y = x + k3 ẋ
 *   with k1 = ζ/(π f), k2 = 1/(2π f)², k3 = r ζ/(2π f), ω = 2π f.
 * - Closed-form damped springs (Ryan Juckett): exact step for
 *   ẍ + 2 ζ ω ẋ + ω² x = 0 about a set-point (under / critical / over).
 * - PD pose control (Neff / Tan et al.): τ = Kp (θd − θ) − Kd ω,
 *   unit-mass gains Kp = ω², Kd = 2 ζ ω (critically damped when ζ = 1).
 */

struct SecondOrderCoeffs {
    float k1 = 0.f;
    float k2 = 1.f;
    float k3 = 0.f;
};

/** Build second-order coefficients from frequency (Hz), damping ζ, response r. */
inline SecondOrderCoeffs makeSecondOrderCoeffs(float frequencyHz, float dampingZeta,
                                               float response) {
    const float f = std::max(frequencyHz, 1e-4f);
    const float z = std::max(dampingZeta, 0.f);
    const float twoPiF = 6.283185307179586f * f;
    SecondOrderCoeffs c;
    c.k1 = z / (3.141592653589793f * f);
    c.k2 = 1.f / (twoPiF * twoPiF);
    c.k3 = response * z / twoPiF;
    return c;
}

/**
 * Stable semi-implicit Euler step for a second-order tracker.
 * Estimates input velocity from consecutive targets when xdEstimate is used by caller.
 * k2 is clamped (k2_stable) so large dt cannot explode the integrator.
 */
inline void stepSecondOrder(float dt, float x, float xd, const SecondOrderCoeffs &c, float &y,
                            float &yd) {
    if (dt <= 0.f) return;
    const float k2Stable = std::max(c.k2, std::max(dt * dt * 0.5f + dt * c.k1 * 0.5f, dt * c.k1));
    y += dt * yd;
    yd += dt * (x + c.k3 * xd - y - c.k1 * yd) / k2Stable;
}

/** Closed-form coefficients for one damped-spring time step (Juckett). */
struct DampedSpringStep {
    float posPos = 1.f;
    float posVel = 0.f;
    float velPos = 0.f;
    float velVel = 1.f;
};

inline DampedSpringStep makeDampedSpringStep(float dt, float angularFrequency, float dampingRatio) {
    DampedSpringStep s;
    if (dt <= 0.f || angularFrequency <= 0.f) return s;

    const float eps   = 1e-4f;
    const float omega = angularFrequency;
    const float zeta  = std::max(dampingRatio, 0.f);

    if (zeta > 1.f + eps) {
        // Over-damped
        const float za = -omega * zeta;
        const float zb = omega * std::sqrt(zeta * zeta - 1.f);
        const float z1 = za - zb;
        const float z2 = za + zb;
        const float e1 = std::exp(z1 * dt);
        const float e2 = std::exp(z2 * dt);
        const float invTwoZb = 1.f / (2.f * zb);
        const float e1Over = e1 * invTwoZb;
        const float e2Over = e2 * invTwoZb;
        const float z1e1   = z1 * e1Over;
        const float z2e2   = z2 * e2Over;
        s.posPos = e1Over * z2 - z2e2 + e2;
        s.posVel = -e1Over + e2Over;
        s.velPos = (z1e1 - z2e2 + e2) * z2;
        s.velVel = -z1e1 + z2e2;
    } else if (zeta < 1.f - eps) {
        // Under-damped
        const float omegaZeta = omega * zeta;
        const float alpha     = omega * std::sqrt(1.f - zeta * zeta);
        const float expTerm   = std::exp(-omegaZeta * dt);
        const float cosTerm   = std::cos(alpha * dt);
        const float sinTerm   = std::sin(alpha * dt);
        const float invAlpha  = 1.f / alpha;
        const float expSin    = expTerm * sinTerm;
        const float expCos    = expTerm * cosTerm;
        const float expOzSin  = expTerm * omegaZeta * sinTerm * invAlpha;
        s.posPos = expCos + expOzSin;
        s.posVel = expSin * invAlpha;
        s.velPos = -expSin * alpha - omegaZeta * expOzSin;
        s.velVel = expCos - expOzSin;
    } else {
        // Critically damped
        const float expTerm     = std::exp(-omega * dt);
        const float timeExp     = dt * expTerm;
        const float timeExpFreq = timeExp * omega;
        s.posPos = timeExpFreq + expTerm;
        s.posVel = timeExp;
        s.velPos = -omega * timeExpFreq;
        s.velVel = -timeExpFreq + expTerm;
    }
    return s;
}

/** Advance position/velocity relative to a set-point using closed-form spring step. */
inline void stepDampedSpring(float dt, float target, float angularFrequency, float dampingRatio,
                             float &pos, float &vel) {
    const DampedSpringStep s = makeDampedSpringStep(dt, angularFrequency, dampingRatio);
    const float oldPos = pos - target;
    const float oldVel = vel;
    const float newPos = oldPos * s.posPos + oldVel * s.posVel;
    const float newVel = oldPos * s.velPos + oldVel * s.velVel;
    pos = newPos + target;
    vel = newVel;
}

/** Unit-mass PD gains from natural frequency ω (rad/s) and damping ratio ζ. */
inline void pdGainsFromOmegaZeta(float omega, float zeta, float &kp, float &kd) {
    const float w = std::max(omega, 0.f);
    const float z = std::max(zeta, 0.f);
    kp = w * w;
    kd = 2.f * z * w;
}

/**
 * Semi-implicit Euler PD step (unit mass):
 * a = Kp (target − y) + Kd (targetVel − yd), then integrate.
 */
inline void stepPd(float dt, float target, float targetVel, float kp, float kd, float &y,
                   float &yd) {
    if (dt <= 0.f) return;
    const float a = kp * (target - y) + kd * (targetVel - yd);
    yd += a * dt;
    y += yd * dt;
}

/** ω (rad/s) from frequency in Hz. */
inline float hzToOmega(float frequencyHz) {
    return 6.283185307179586f * std::max(frequencyHz, 0.f);
}

}  // namespace eve::animation
