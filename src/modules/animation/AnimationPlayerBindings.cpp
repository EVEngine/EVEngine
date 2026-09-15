#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimationBindings.h"

namespace eve::animation {
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
