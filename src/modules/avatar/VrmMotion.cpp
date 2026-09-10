#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "avatar/AvatarInstance.h"
#include "avatar/VrmRuntime.h"

namespace eve::avatar {
namespace {
glm::vec3 vec(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }
glm::mat4 matrix(const animation::AnimPose& pose, int bone) {
    glm::mat4 m(1);
    pose.getWorldMatrix(bone, &m[0][0]);
    return m;
}
glm::quat quaternion(const animation::TransformTRS& t) { return {t.qw, t.qx, t.qy, t.qz}; }
glm::vec3 unit(glm::vec3 v, glm::vec3 fallback = {0, 1, 0}) {
    float n = glm::length(v);
    return n > 1e-7f ? v / n : fallback;
}
void rotate(animation::AnimPose& pose, int bone, glm::quat q) {
    q = glm::normalize(q);
    pose.setLocalRotation(bone, q.x, q.y, q.z, q.w);
}
float mapped(float value, const VrmLookAt::Range& r) {
    return std::min(std::abs(value) / std::max(r.input, .0001f), 1.f) * r.output;
}
}  // namespace
void VrmRuntime::gaze(AvatarInstance& avatar, animation::AnimPose& pose, const glm::mat4& modelWorld, bool enabled,
                      glm::vec3 target, float weight) {
    for (const auto& [bone, angles] : rotations) {
        glm::quat delta = glm::angleAxis(angles[0], glm::vec3(0, 1, 0)) *
                          glm::angleAxis(angles[1], glm::vec3(1, 0, 0)) * glm::angleAxis(angles[2], glm::vec3(0, 0, 1));
        rotate(pose, bone, quaternion(pose.local(bone)) * delta);
    }
    pose.computeWorld(skeleton.get());
    gazeWeights_.clear();
    if (!enabled || weight <= 0 || !document.look.present || !document.humanoid.contains("head")) return;
    if (!document.look.expression) {
        float suppression = 0;
        for (const auto& expression : document.expressions) {
            float value = std::clamp(avatar.getParameter(expression.name), 0.f, 1.f);
            if (expression.binary) value = value > .5f ? 1.f : 0.f;
            if (expression.overrideLookAt == "block" && value > 0)
                suppression = 1;
            else if (expression.overrideLookAt == "blend")
                suppression += value;
        }
        weight *= 1 - std::clamp(suppression, 0.f, 1.f);
    }
    const int           head = nodeBones[document.humanoid.at("head")];
    animation::AnimPose bind(skeleton->getBoneCount());
    skeleton->applyBindPose(&bind);
    bind.computeWorld(skeleton.get());
    const auto&     h           = pose.world(head);
    const glm::quat rest        = quaternion(bind.world(head));
    const glm::quat orientation = quaternion(h) * glm::inverse(rest);
    glm::vec3       origin      = glm::vec3(matrix(pose, head) * glm::vec4(vec(document.look.offset), 1));
    glm::vec3 local = glm::inverse(orientation) * (glm::vec3(glm::inverse(modelWorld) * glm::vec4(target, 1)) - origin);
    const float yaw = glm::degrees(std::atan2(local.x, local.z));
    const float pitch    = glm::degrees(std::atan2(-local.y, std::hypot(local.x, local.z)));
    const auto& look     = document.look;
    const float vertical = mapped(pitch, pitch >= 0 ? look.down : look.up) * weight;
    if (look.expression) {
        gazeWeights_[yaw >= 0 ? "lookLeft" : "lookRight"] = mapped(yaw, look.outer) * weight;
        gazeWeights_[pitch >= 0 ? "lookDown" : "lookUp"]  = vertical;
        return;
    }
    for (auto semantic : {"leftEye", "rightEye"}) {
        auto it = document.humanoid.find(semantic);
        if (it == document.humanoid.end()) continue;
        const int   bone       = nodeBones[it->second];
        const bool  outer      = (yaw >= 0) == (std::string_view(semantic) == "leftEye");
        const float horizontal = mapped(yaw, outer ? look.outer : look.inner) * weight;
        glm::quat   delta      = glm::angleAxis(glm::radians(std::copysign(horizontal, yaw)), glm::vec3(0, 1, 0)) *
                          glm::angleAxis(glm::radians(std::copysign(vertical, pitch)), glm::vec3(1, 0, 0));
        // Convert the model-axis gaze into the authored eye's local rest axes.
        const glm::quat eyeRest = quaternion(bind.world(bone));
        const auto      current = quaternion(pose.local(bone));
        rotate(pose, bone, current * glm::inverse(eyeRest) * delta * eyeRest);
    }
    pose.computeWorld(skeleton.get());
}
void VrmRuntime::springs(animation::AnimPose& pose, float dt) {
    if (!std::isfinite(dt) || dt <= 0) return;
    // A long pause/teleport restarts particles; a bounded dt avoids explosive catch-up.
    const bool reset = dt > .25f;
    dt               = std::min(dt, .05f);
    for (size_t s = 0; s < document.springs.size(); ++s) {
        const auto&     spring = document.springs[s];
        glm::mat4       center = spring.center >= 0 ? world * matrix(pose, nodeBones[spring.center]) : glm::mat4(1);
        const glm::mat4 inverseCenter = glm::inverse(center);
        for (size_t j = 0; j + 1 < spring.joints.size(); ++j) {
            const auto&     joint = spring.joints[j];
            const int       bone = nodeBones[joint.node], tailBone = nodeBones[spring.joints[j + 1].node];
            const glm::mat4 boneWorld = world * matrix(pose, bone);
            const glm::vec3 origin    = glm::vec3(boneWorld[3]);
            const glm::vec3 restTail  = glm::vec3(world * matrix(pose, tailBone)[3]);
            const glm::vec3 axis      = unit(restTail - origin);
            const float     length    = glm::length(restTail - origin);
            if (length < 1e-7f) continue;
            auto& particle = particles_[s][j];
            if (!particle.initialized || reset) {
                particle.current = particle.previous = glm::vec3(inverseCenter * glm::vec4(restTail, 1));
                particle.initialized                 = true;
            }
            const glm::vec3 current  = glm::vec3(center * glm::vec4(particle.current, 1));
            const glm::vec3 previous = glm::vec3(center * glm::vec4(particle.previous, 1));
            glm::vec3       next = current + (current - previous) * (1 - joint.drag) + axis * (dt * joint.stiffness) +
                             vec(joint.gravity) * (dt * joint.gravityPower);
            next = origin + unit(next - origin, axis) * length;
            for (int ci : spring.colliders) {
                const auto&     c       = document.colliders[ci];
                const glm::mat4 cm      = world * matrix(pose, nodeBones[c.node]);
                glm::vec3       closest = glm::vec3(cm * glm::vec4(vec(c.offset), 1));
                if (c.capsule) {
                    glm::vec3 segment = glm::vec3(cm * glm::vec4(vec(c.tail), 1)) - closest;
                    float     sq      = glm::dot(segment, segment);
                    if (sq > 1e-12f) closest += segment * std::clamp(glm::dot(next - closest, segment) / sq, 0.f, 1.f);
                }
                float     radius = c.radius + joint.radius;
                glm::vec3 delta  = next - closest;
                if (glm::dot(delta, delta) < radius * radius) {
                    next = closest + unit(delta, axis) * radius;
                    next = origin + unit(next - origin, axis) * length;
                }
            }
            particle.previous           = particle.current;
            particle.current            = glm::vec3(inverseCenter * glm::vec4(next, 1));
            const glm::vec3 localAxis   = unit(glm::vec3(glm::inverse(boneWorld) * glm::vec4(restTail, 1)));
            const glm::vec3 localTarget = unit(glm::vec3(glm::inverse(boneWorld) * glm::vec4(next, 1)), localAxis);
            rotate(pose, bone, quaternion(pose.local(bone)) * glm::rotation(localAxis, localTarget));
            pose.computeWorld(skeleton.get());
        }
    }
}
}  // namespace eve::avatar
