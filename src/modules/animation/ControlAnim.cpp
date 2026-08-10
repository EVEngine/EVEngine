#include "animation/ControlAnim.h"

#include "common/Exception.h"

#include <algorithm>

namespace eve::animation {

ControlAnim::ControlAnim(float frequencyHz, float dampingZeta, float response)
    : frequencyHz_(frequencyHz), dampingZeta_(dampingZeta), response_(response) {
    refreshCoeffs();
}

void ControlAnim::setFrequency(float frequencyHz) {
    frequencyHz_ = frequencyHz;
    refreshCoeffs();
}

void ControlAnim::setDamping(float dampingZeta) {
    dampingZeta_ = dampingZeta;
    refreshCoeffs();
}

void ControlAnim::setResponse(float response) {
    response_ = response;
    refreshCoeffs();
}

void ControlAnim::setIntegrator(const std::string &kind) {
    if (kind == "secondOrder") {
        integrator_ = Integrator::SecondOrder;
    } else if (kind == "spring") {
        integrator_ = Integrator::Spring;
    } else if (kind == "pd") {
        integrator_ = Integrator::Pd;
    } else {
        throw Exception("ControlAnim::setIntegrator: unknown kind '%s' (expected secondOrder|spring|pd)",
                        kind.c_str());
    }
}

std::string ControlAnim::getIntegrator() const {
    switch (integrator_) {
        case Integrator::SecondOrder: return "secondOrder";
        case Integrator::Spring:      return "spring";
        case Integrator::Pd:          return "pd";
    }
    return "secondOrder";
}

void ControlAnim::refreshCoeffs() {
    coeffs_ = makeSecondOrderCoeffs(frequencyHz_, dampingZeta_, response_);
    pdGainsFromOmegaZeta(hzToOmega(frequencyHz_), dampingZeta_, kp_, kd_);
}

ControlAnim::Channel &ControlAnim::ensure(const std::string &name) {
    auto it = channels_.find(name);
    if (it != channels_.end()) return it->second;
    Channel ch;
    channels_.emplace(name, ch);
    order_.push_back(name);
    return channels_[name];
}

void ControlAnim::set(const std::string &name, float value) {
    Channel &ch = ensure(name);
    ch.y          = value;
    ch.yd         = 0.f;
    ch.x          = value;
    ch.xp         = value;
    ch.xdExplicit = 0.f;
    ch.hasXd      = false;
    ch.hasPrevX   = true;
}

void ControlAnim::setTarget(const std::string &name, float value) {
    Channel &ch = ensure(name);
    if (!ch.hasPrevX) {
        ch.y        = value;
        ch.yd       = 0.f;
        ch.xp       = value;
        ch.hasPrevX = true;
    }
    ch.x = value;
}

void ControlAnim::setTargetVelocity(const std::string &name, float velocity) {
    Channel &ch  = ensure(name);
    ch.xdExplicit = velocity;
    ch.hasXd      = true;
}

void ControlAnim::impulse(const std::string &name, float deltaVelocity) {
    Channel &ch = ensure(name);
    ch.yd += deltaVelocity;
}

bool ControlAnim::has(const std::string &name) const {
    return channels_.find(name) != channels_.end();
}

float ControlAnim::get(const std::string &name) const {
    auto it = channels_.find(name);
    return it == channels_.end() ? 0.f : it->second.y;
}

float ControlAnim::getVelocity(const std::string &name) const {
    auto it = channels_.find(name);
    return it == channels_.end() ? 0.f : it->second.yd;
}

float ControlAnim::getTarget(const std::string &name) const {
    auto it = channels_.find(name);
    return it == channels_.end() ? 0.f : it->second.x;
}

void ControlAnim::clear() {
    channels_.clear();
    order_.clear();
}

void ControlAnim::remove(const std::string &name) {
    channels_.erase(name);
    order_.erase(std::remove(order_.begin(), order_.end(), name), order_.end());
}

std::string ControlAnim::getPropertyName(int index) const {
    if (index < 0 || index >= static_cast<int>(order_.size())) return {};
    return order_[static_cast<size_t>(index)];
}

void ControlAnim::update(float dt) {
    if (dt <= 0.f) return;
    const float omega = hzToOmega(frequencyHz_);

    for (auto &kv : channels_) {
        Channel &ch = kv.second;
        float xd    = 0.f;
        if (ch.hasXd) {
            xd = ch.xdExplicit;
        } else if (ch.hasPrevX && dt > 1e-8f) {
            xd = (ch.x - ch.xp) / dt;
        }
        ch.xp       = ch.x;
        ch.hasPrevX = true;

        switch (integrator_) {
            case Integrator::SecondOrder:
                stepSecondOrder(dt, ch.x, xd, coeffs_, ch.y, ch.yd);
                break;
            case Integrator::Spring:
                stepDampedSpring(dt, ch.x, omega, dampingZeta_, ch.y, ch.yd);
                break;
            case Integrator::Pd:
                stepPd(dt, ch.x, xd, kp_, kd_, ch.y, ch.yd);
                break;
        }
    }
}

}  // namespace eve::animation
