#pragma once

#include "animation/AnimControlMath.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

/**
 * @brief Control-theory procedural animation for named scalar properties.
 *
 * Tracks continuous targets with a second-order LTI system (default), a
 * closed-form damped spring, or an explicit unit-mass PD law.
 * Script type: `ControlAnim`.
 *
 * Integrator kinds (string, no overloads):
 * - "secondOrder" — ÿ + k1 ẏ + k2 y = x + k3 ẋ  (f, ζ, r)
 * - "spring"      — closed-form damped spring about set-point (f, ζ; r ignored)
 * - "pd"          — τ/m = Kp(x − y) + Kd(ẋ − ẏ) with Kp=ω², Kd=2ζω
 */
class ControlAnim {
public:
    ControlAnim(float frequencyHz = 3.f, float dampingZeta = 1.f, float response = 1.f);
    ~ControlAnim() = default;

    ControlAnim(const ControlAnim &)            = delete;
    ControlAnim &operator=(const ControlAnim &) = delete;

    void  setFrequency(float frequencyHz);
    float getFrequency() const { return frequencyHz_; }
    void  setDamping(float dampingZeta);
    float getDamping() const { return dampingZeta_; }
    void  setResponse(float response);
    float getResponse() const { return response_; }

    /** @brief Integrator: "secondOrder" | "spring" | "pd". Invalid → exception. */
    void        setIntegrator(const std::string &kind);
    std::string getIntegrator() const;

    /** @brief Create/update a channel: current value snaps to `value`, velocity cleared. */
    void set(const std::string &name, float value);
    /** @brief Move the tracking target (does not snap state). Creates channel if missing. */
    void setTarget(const std::string &name, float value);
    /** @brief Optional explicit target velocity (used by secondOrder / pd). */
    void setTargetVelocity(const std::string &name, float velocity);
    /** @brief Inject an instantaneous velocity impulse (procedural “hit”). */
    void impulse(const std::string &name, float deltaVelocity);

    bool  has(const std::string &name) const;
    float get(const std::string &name) const;
    float getVelocity(const std::string &name) const;
    float getTarget(const std::string &name) const;

    void clear();
    void remove(const std::string &name);
    int  getPropertyCount() const { return static_cast<int>(order_.size()); }
    std::string getPropertyName(int index) const;

    /** @brief Advance all channels by dt seconds. */
    void update(float dt);

private:
    enum class Integrator { SecondOrder, Spring, Pd };

    struct Channel {
        float y          = 0.f;
        float yd         = 0.f;
        float x          = 0.f;
        float xp         = 0.f;
        float xdExplicit = 0.f;
        bool  hasXd      = false;
        bool  hasPrevX   = false;
    };

    void refreshCoeffs();
    Channel &ensure(const std::string &name);

    float frequencyHz_  = 3.f;
    float dampingZeta_  = 1.f;
    float response_     = 1.f;
    Integrator integrator_ = Integrator::SecondOrder;
    SecondOrderCoeffs coeffs_{};
    float kp_ = 0.f;
    float kd_ = 0.f;

    std::unordered_map<std::string, Channel> channels_;
    std::vector<std::string> order_;
};

}  // namespace eve::animation
