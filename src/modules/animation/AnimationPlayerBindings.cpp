#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <simplesquirrel/simplesquirrel.hpp>
#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimationBindings.h"
#include "animation/RootMotionPolicy.h"

namespace eve::animation {
namespace {

RootMotionLockAxes parseLockAxes(const std::string& name) {
    if (name == "none" || name.empty()) return RootMotionLockAxes::None;
    if (name == "x") return RootMotionLockAxes::X;
    if (name == "y" || name == "vertical") return RootMotionLockAxes::VerticalY;
    if (name == "z") return RootMotionLockAxes::Z;
    if (name == "xz" || name == "horizontal") return RootMotionLockAxes::HorizontalXZ;
    if (name == "xy") return RootMotionLockAxes::X | RootMotionLockAxes::Y;
    if (name == "yz") return RootMotionLockAxes::Y | RootMotionLockAxes::Z;
    if (name == "xyz" || name == "all") return RootMotionLockAxes::All;
    throw std::runtime_error("unknown root motion lockAxes: " + name);
}

std::string formatLockAxes(RootMotionLockAxes mask) {
    const auto bits = static_cast<std::uint8_t>(mask);
    if (bits == 0) return "none";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::All)) return "xyz";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::HorizontalXZ)) return "xz";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::VerticalY)) return "y";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::X)) return "x";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::Z)) return "z";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::X | RootMotionLockAxes::Y)) return "xy";
    if (bits == static_cast<std::uint8_t>(RootMotionLockAxes::Y | RootMotionLockAxes::Z)) return "yz";
    return "xyz";
}

RootMotionApplySpace parseApplySpace(const std::string& name) {
    if (name == "boneLocal" || name == "local") return RootMotionApplySpace::BoneLocal;
    if (name == "characterFacing" || name == "facing") return RootMotionApplySpace::CharacterFacing;
    throw std::runtime_error("unknown root motion applySpace: " + name);
}

std::string formatApplySpace(RootMotionApplySpace space) {
    return space == RootMotionApplySpace::CharacterFacing ? "characterFacing" : "boneLocal";
}

ssq::Table applyPolicy(HSQUIRRELVM vm, AnimPlayer* self, const RootMotionPolicy& policy) {
    ssq::Table result(vm);
    auto       applied = self->setRootMotionPolicy(policy);
    result.set("ok", applied.ok());
    result.set("message", applied.status().describe());
    return result;
}

}  // namespace

void exposeAnimPlayerBindings(ssq::Table& table) {
    auto player = table.addClass<AnimPlayer>(
        "AnimPlayer", std::function<AnimPlayer*()>([]() -> AnimPlayer* { return nullptr; }), true);
    player.addFunc("play", &AnimPlayer::play);
    player.addFunc("crossFade", &AnimPlayer::crossFade);
    player.addFunc("stop", &AnimPlayer::stop);
    player.addFunc("pause", &AnimPlayer::pause);
    player.addFunc("resume", &AnimPlayer::resume);
    player.addFunc("setSpeed", &AnimPlayer::setSpeed);
    player.addFunc("getSpeed", &AnimPlayer::getSpeed);
    player.addFunc("setTime", &AnimPlayer::setTime);
    player.addFunc("getTime", &AnimPlayer::getTime);
    player.addFunc("setLoop", &AnimPlayer::setLoop);
    player.addFunc("getLoop", &AnimPlayer::getLoop);
    player.addFunc("isPlaying", &AnimPlayer::isPlaying);
    player.addFunc("isPaused", &AnimPlayer::isPaused);
    player.addFunc("getPose", &AnimPlayer::getPose);
    player.addFunc("getClip", &AnimPlayer::getClip);
    player.addFunc("setRootMotionBone", &AnimPlayer::setRootMotionBone);
    player.addFunc("getRootMotionBone", &AnimPlayer::getRootMotionBone);
    player.addFunc("setRootMotionLockAxes",
                   [vm = table.getHandle()](AnimPlayer* self, const std::string& axes) {
                       RootMotionPolicy policy = self->getRootMotionPolicy();
                       policy.lockAxes         = parseLockAxes(axes);
                       return applyPolicy(vm, self, policy);
                   });
    player.addFunc("setRootMotionApplySpace",
                   [vm = table.getHandle()](AnimPlayer* self, const std::string& space) {
                       RootMotionPolicy policy = self->getRootMotionPolicy();
                       policy.applySpace       = parseApplySpace(space);
                       return applyPolicy(vm, self, policy);
                   });
    player.addFunc("setBakeRootTranslationIntoPose",
                   [vm = table.getHandle()](AnimPlayer* self, bool enabled) {
                       RootMotionPolicy policy         = self->getRootMotionPolicy();
                       policy.bakeTranslationIntoPose  = enabled;
                       return applyPolicy(vm, self, policy);
                   });
    player.addFunc("setBakeRootRotationIntoPose",
                   [vm = table.getHandle()](AnimPlayer* self, bool enabled) {
                       RootMotionPolicy policy     = self->getRootMotionPolicy();
                       policy.bakeRotationIntoPose = enabled;
                       return applyPolicy(vm, self, policy);
                   });
    player.addFunc("setRootMotionLockRotation",
                   [vm = table.getHandle()](AnimPlayer* self, bool enabled) {
                       RootMotionPolicy policy = self->getRootMotionPolicy();
                       policy.lockRotation     = enabled;
                       return applyPolicy(vm, self, policy);
                   });
    player.addFunc("getRootMotionPolicy", [vm = table.getHandle()](AnimPlayer* self) {
        const RootMotionPolicy policy = self->getRootMotionPolicy();
        ssq::Table             tableOut(vm);
        tableOut.set("lockAxes", formatLockAxes(policy.lockAxes));
        tableOut.set("applySpace", formatApplySpace(policy.applySpace));
        tableOut.set("bakeTranslationIntoPose", policy.bakeTranslationIntoPose);
        tableOut.set("bakeRotationIntoPose", policy.bakeRotationIntoPose);
        tableOut.set("lockRotation", policy.lockRotation);
        tableOut.set("characterYaw", policy.characterYaw);
        return tableOut;
    });
    player.addFunc("setRootMotionCharacterYaw", [vm = table.getHandle()](AnimPlayer* self, float yaw) {
        ssq::Table result(vm);
        auto       applied = self->setRootMotionCharacterYaw(yaw);
        result.set("ok", applied.ok());
        result.set("message", applied.status().describe());
        return result;
    });
    player.addFunc("getRootMotionX", &AnimPlayer::getRootMotionX);
    player.addFunc("getRootMotionY", &AnimPlayer::getRootMotionY);
    player.addFunc("getRootMotionZ", &AnimPlayer::getRootMotionZ);
    player.addFunc("getRootMotionRotationX", &AnimPlayer::getRootMotionRotationX);
    player.addFunc("getRootMotionRotationY", &AnimPlayer::getRootMotionRotationY);
    player.addFunc("getRootMotionRotationZ", &AnimPlayer::getRootMotionRotationZ);
    player.addFunc("getRootMotionRotationW", &AnimPlayer::getRootMotionRotationW);
    player.addFunc("consumeEvent", &AnimPlayer::consumeEvent);
    player.addFunc("setUpdateRate", &AnimPlayer::setUpdateRate);
    player.addFunc("getUpdateRate", &AnimPlayer::getUpdateRate);
    player.addFunc("update", &AnimPlayer::update);

    player.addFunc("getEventCount", &AnimPlayer::getEventCount);
    player.addFunc("getEventName", &AnimPlayer::getEventName);
    player.addFunc("getEventPayload", &AnimPlayer::getEventPayload);
    player.addFunc("clearEvents", &AnimPlayer::clearEvents);
}
}  // namespace eve::animation
