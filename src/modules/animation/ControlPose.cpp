#include "animation/ControlPose.h"

#include "animation/AnimSkeleton.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::animation {

ControlPose::ControlPose(AnimSkeleton *skeleton) : skeleton_(skeleton) {
    refreshCoeffs();
    const int n = skeleton_ ? skeleton_->getBoneCount() : 0;
    ensureBones(n);
    if (skeleton_ && n > 0) {
        skeleton_->applyBindPose(&pose_);
        skeleton_->applyBindPose(&target_);
        readTargetIntoState(&target_, true);
        hasTarget_ = true;
        writePoseFromState();
    }
}

void ControlPose::setFrequency(float frequencyHz) {
    frequencyHz_ = frequencyHz;
    refreshCoeffs();
}

void ControlPose::setDamping(float dampingZeta) {
    dampingZeta_ = dampingZeta;
    refreshCoeffs();
}

void ControlPose::setResponse(float response) {
    response_ = response;
    refreshCoeffs();
}

void ControlPose::setIntegrator(const std::string &kind) {
    if (kind == "secondOrder") {
        integrator_ = Integrator::SecondOrder;
    } else if (kind == "spring") {
        integrator_ = Integrator::Spring;
    } else if (kind == "pd") {
        integrator_ = Integrator::Pd;
    } else {
        throw Exception("ControlPose::setIntegrator: unknown kind '%s' (expected secondOrder|spring|pd)",
                        kind.c_str());
    }
}

std::string ControlPose::getIntegrator() const {
    switch (integrator_) {
        case Integrator::SecondOrder: return "secondOrder";
        case Integrator::Spring:      return "spring";
        case Integrator::Pd:          return "pd";
    }
    return "secondOrder";
}

void ControlPose::refreshCoeffs() {
    coeffs_ = makeSecondOrderCoeffs(frequencyHz_, dampingZeta_, response_);
    pdGainsFromOmegaZeta(hzToOmega(frequencyHz_), dampingZeta_, kp_, kd_);
}

void ControlPose::ensureBones(int count) {
    if (count < 0) count = 0;
    // AnimPose::resize always resets locals to identity — only call when size changes.
    if (pose_.getBoneCount() != count) pose_.resize(count);
    if (target_.getBoneCount() != count) target_.resize(count);
    if (static_cast<int>(bones_.size()) != count) {
        bones_.resize(static_cast<size_t>(count));
    }
}

void ControlPose::setBoneWeight(int boneIndex, float weight) {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) return;
    bones_[static_cast<size_t>(boneIndex)].weight = clampf(weight, 0.f, 1.f);
}

float ControlPose::getBoneWeight(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) return 0.f;
    return bones_[static_cast<size_t>(boneIndex)].weight;
}

void ControlPose::readTargetIntoState(const AnimPose *target, bool snap) {
    if (!target) return;
    const int n = target->getBoneCount();
    ensureBones(n);
    for (int i = 0; i < n; ++i) {
        const TransformTRS &t = target->local(i);
        BoneState &b          = bones_[static_cast<size_t>(i)];

        auto assign = [snap](ScalarState &s, float value) {
            s.x = value;
            if (!s.hasPrev || snap) {
                s.y       = value;
                s.yd      = 0.f;
                s.xp      = value;
                s.hasPrev = true;
            }
        };

        assign(b.px, t.px);
        assign(b.py, t.py);
        assign(b.pz, t.pz);

        // Shortest-path quaternion: flip target if needed relative to current.
        float tqx = t.qx, tqy = t.qy, tqz = t.qz, tqw = t.qw;
        if (b.qw.hasPrev) {
            const float dot = b.qx.y * tqx + b.qy.y * tqy + b.qz.y * tqz + b.qw.y * tqw;
            if (dot < 0.f) {
                tqx = -tqx;
                tqy = -tqy;
                tqz = -tqz;
                tqw = -tqw;
            }
        }
        assign(b.qx, tqx);
        assign(b.qy, tqy);
        assign(b.qz, tqz);
        assign(b.qw, tqw);

        assign(b.sx, t.sx);
        assign(b.sy, t.sy);
        assign(b.sz, t.sz);
    }
}

void ControlPose::setTargetPose(const AnimPose *target) {
    if (!target) {
        throw Exception("ControlPose::setTargetPose: target is null");
    }
    target_.copyFrom(target);
    readTargetIntoState(&target_, false);
    hasTarget_ = true;
    writePoseFromState();
}

void ControlPose::snapToTarget() {
    if (!hasTarget_) return;
    readTargetIntoState(&target_, true);
    writePoseFromState();
}

AnimPose *ControlPose::getPose() {
    writePoseFromState();
    return &pose_;
}

AnimPose *ControlPose::getTargetPose() { return &target_; }

void ControlPose::stepScalar(float dt, float xd, ScalarState &s) {
    const float omega = hzToOmega(frequencyHz_);
    switch (integrator_) {
        case Integrator::SecondOrder:
            stepSecondOrder(dt, s.x, xd, coeffs_, s.y, s.yd);
            break;
        case Integrator::Spring:
            stepDampedSpring(dt, s.x, omega, dampingZeta_, s.y, s.yd);
            break;
        case Integrator::Pd:
            stepPd(dt, s.x, xd, kp_, kd_, s.y, s.yd);
            break;
    }
}

void ControlPose::writePoseFromState() {
    const int n = static_cast<int>(bones_.size());
    pose_.resize(n);
    for (int i = 0; i < n; ++i) {
        const BoneState &b = bones_[static_cast<size_t>(i)];
        TransformTRS trs;
        trs.px = b.px.y;
        trs.py = b.py.y;
        trs.pz = b.pz.y;
        trs.qx = b.qx.y;
        trs.qy = b.qy.y;
        trs.qz = b.qz.y;
        trs.qw = b.qw.y;
        trs.sx = b.sx.y;
        trs.sy = b.sy.y;
        trs.sz = b.sz.y;
        trs.normalizeRotation();

        if (b.weight < 1.f - 1e-6f) {
            // Blend dynamics output toward hard target.
            TransformTRS hard = target_.local(i);
            // Shortest path for hard target vs dynamics result.
            const float dot =
                trs.qx * hard.qx + trs.qy * hard.qy + trs.qz * hard.qz + trs.qw * hard.qw;
            if (dot < 0.f) {
                hard.qx = -hard.qx;
                hard.qy = -hard.qy;
                hard.qz = -hard.qz;
                hard.qw = -hard.qw;
            }
            trs = blendTRS(hard, trs, b.weight);
        }
        pose_.local(i) = trs;
    }
}

void ControlPose::update(float dt) {
    if (dt <= 0.f || !hasTarget_) return;

    // Refresh targets from stored target_ (weights / integrator may have changed).
    const int n = target_.getBoneCount();
    ensureBones(n);
    for (int i = 0; i < n; ++i) {
        const TransformTRS &t = target_.local(i);
        BoneState &b          = bones_[static_cast<size_t>(i)];

        auto prep = [](ScalarState &s, float value) {
            if (!s.hasPrev) {
                s.y       = value;
                s.yd      = 0.f;
                s.xp      = value;
                s.hasPrev = true;
            }
            s.x = value;
        };

        prep(b.px, t.px);
        prep(b.py, t.py);
        prep(b.pz, t.pz);

        float tqx = t.qx, tqy = t.qy, tqz = t.qz, tqw = t.qw;
        const float dot = b.qx.y * tqx + b.qy.y * tqy + b.qz.y * tqz + b.qw.y * tqw;
        if (dot < 0.f) {
            tqx = -tqx;
            tqy = -tqy;
            tqz = -tqz;
            tqw = -tqw;
        }
        prep(b.qx, tqx);
        prep(b.qy, tqy);
        prep(b.qz, tqz);
        prep(b.qw, tqw);

        prep(b.sx, t.sx);
        prep(b.sy, t.sy);
        prep(b.sz, t.sz);

        auto advance = [&](ScalarState &s) {
            float xd = 0.f;
            if (s.hasPrev && dt > 1e-8f) xd = (s.x - s.xp) / dt;
            s.xp = s.x;
            stepScalar(dt, xd, s);
        };

        if (b.weight <= 1e-6f) {
            // Hard follow
            auto hard = [](ScalarState &s) {
                s.y  = s.x;
                s.yd = 0.f;
                s.xp = s.x;
            };
            hard(b.px);
            hard(b.py);
            hard(b.pz);
            hard(b.qx);
            hard(b.qy);
            hard(b.qz);
            hard(b.qw);
            hard(b.sx);
            hard(b.sy);
            hard(b.sz);
        } else {
            advance(b.px);
            advance(b.py);
            advance(b.pz);
            advance(b.qx);
            advance(b.qy);
            advance(b.qz);
            advance(b.qw);
            // Renormalize quaternion state after component integration.
            {
                float len = std::sqrt(b.qx.y * b.qx.y + b.qy.y * b.qy.y + b.qz.y * b.qz.y +
                                      b.qw.y * b.qw.y);
                if (len > 1e-8f) {
                    const float inv = 1.f / len;
                    b.qx.y *= inv;
                    b.qy.y *= inv;
                    b.qz.y *= inv;
                    b.qw.y *= inv;
                } else {
                    b.qx.y = 0.f;
                    b.qy.y = 0.f;
                    b.qz.y = 0.f;
                    b.qw.y = 1.f;
                }
            }
            advance(b.sx);
            advance(b.sy);
            advance(b.sz);
        }
    }
    writePoseFromState();
}

}  // namespace eve::animation
