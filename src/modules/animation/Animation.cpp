#include "animation/Animation.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/AnimStateMachine.h"
#include "animation/AnimTrail.h"
#include "animation/ControlAnim.h"
#include "animation/ControlPose.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::animation {

Module_IMPL(Animation, new Animation());

Animation::~Animation() {
    // Tweens may outlive the module if script GC still holds them; detach.
    for (Tween *t : tweens_) {
        if (t) t->setOwner(nullptr);
    }
    tweens_.clear();
}

Tween *Animation::newTween(float duration) {
    auto *t = new Tween(duration);
    registerTween(t);
    return t;
}

AnimSkeleton *Animation::newSkeleton() { return new AnimSkeleton(); }

AnimClip *Animation::newClip(const std::string &name) { return new AnimClip(name); }

AnimPose *Animation::newPose(int boneCount) { return new AnimPose(boneCount); }

AnimPlayer *Animation::newPlayer(AnimSkeleton *skeleton) { return new AnimPlayer(skeleton); }

AnimStateMachine *Animation::newStateMachine(AnimSkeleton *skeleton) {
    return new AnimStateMachine(skeleton);
}

MotionDatabase *Animation::newMotionDatabase(AnimSkeleton *skeleton) {
    return new MotionDatabase(skeleton);
}

MotionMatcher *Animation::newMotionMatcher(AnimSkeleton *skeleton, MotionDatabase *database) {
    return new MotionMatcher(skeleton, database);
}

ControlAnim *Animation::newControlAnim(float frequencyHz, float dampingZeta, float response) {
    return new ControlAnim(frequencyHz, dampingZeta, response);
}

ControlPose *Animation::newControlPose(AnimSkeleton *skeleton) { return new ControlPose(skeleton); }

AnimSkeleton *Animation::newSkeletonFromModel(eve::model3d::ModelData *model) {
    return AnimImporter::loadSkeletonFromModel(model);
}

AnimClip *Animation::newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                      int animIndex) {
    return AnimImporter::loadClipFromModel(model, skeleton, animIndex);
}

AnimSkeleton *Animation::newSkeletonFromEvaFile(const std::string &path) {
    AnimSkeleton *sk = nullptr;
    AnimClip *clip   = nullptr;
    AnimImporter::importEvaFile(path, &sk, &clip);
    delete clip;
    return sk;
}

AnimClip *Animation::newClipFromEvaFile(const std::string &path) {
    AnimSkeleton *sk = nullptr;
    AnimClip *clip   = nullptr;
    AnimImporter::importEvaFile(path, &sk, &clip);
    delete sk;
    return clip;
}

AnimSkin *Animation::newSkinFromModel(eve::model3d::ModelData *model, int meshIndex,
                                      AnimSkeleton *skeleton) {
    return AnimSkin::fromModel(model, meshIndex, skeleton);
}

AnimTrail *Animation::newTrail(int capacity) { return new AnimTrail(capacity); }

void Animation::registerTween(Tween *t) {
    if (!t) return;
    t->setOwner(this);
    if (std::find(tweens_.begin(), tweens_.end(), t) == tweens_.end()) tweens_.push_back(t);
}

void Animation::unregisterTween(Tween *t) {
    if (!t) return;
    auto it = std::find(tweens_.begin(), tweens_.end(), t);
    if (it != tweens_.end()) tweens_.erase(it);
    if (t->owner() == this) t->setOwner(nullptr);
}

void Animation::update(float dt) {
    // Copy pointer list: a tween destructor during update must not invalidate iteration.
    std::vector<Tween *> snapshot = tweens_;
    for (Tween *t : snapshot) {
        if (!t) continue;
        if (t->isActive()) t->update(dt);
    }
}

int Animation::getActiveCount() const {
    int n = 0;
    for (const Tween *t : tweens_) {
        if (t && t->isActive()) ++n;
    }
    return n;
}

void Animation::clearFinished() {
    auto it = tweens_.begin();
    while (it != tweens_.end()) {
        Tween *t = *it;
        if (!t || t->isFinished() || t->isStopped()) {
            if (t && t->owner() == this) t->setOwner(nullptr);
            it = tweens_.erase(it);
        } else {
            ++it;
        }
    }
}

void Animation::clearAll() {
    for (Tween *t : tweens_) {
        if (t && t->owner() == this) t->setOwner(nullptr);
    }
    tweens_.clear();
}

void Animation::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Animation::create, false);
    expose(cls);

    auto tw = table.addClass<Tween>(
        "Tween", std::function<Tween *()>([]() -> Tween * { return nullptr; }), true);
    tw.addFunc("setFrom", &Tween::setFrom);
    tw.addFunc("setTo", &Tween::setTo);
    tw.addFunc("setDelta", &Tween::setDelta);
    tw.addFunc("setFromAngle", &Tween::setFromAngle);
    tw.addFunc("setToAngle", &Tween::setToAngle);
    tw.addFunc("setDeltaAngle", &Tween::setDeltaAngle);
    tw.addFunc("has", &Tween::has);
    tw.addFunc("get", &Tween::get);
    tw.addFunc("getFrom", &Tween::getFrom);
    tw.addFunc("getTo", &Tween::getTo);
    tw.addFunc("getDelta", &Tween::getDelta);
    tw.addFunc("setDuration", &Tween::setDuration);
    tw.addFunc("getDuration", &Tween::getDuration);
    tw.addFunc("setDelay", &Tween::setDelay);
    tw.addFunc("getDelay", &Tween::getDelay);
    tw.addFunc("setEase", &Tween::setEase);
    tw.addFunc("getEase", &Tween::getEase);
    tw.addFunc("setRepeat", &Tween::setRepeat);
    tw.addFunc("getRepeat", &Tween::getRepeat);
    tw.addFunc("setYoyo", &Tween::setYoyo);
    tw.addFunc("getYoyo", &Tween::getYoyo);
    tw.addFunc("start", &Tween::start);
    tw.addFunc("pause", &Tween::pause);
    tw.addFunc("resume", &Tween::resume);
    tw.addFunc("stop", &Tween::stop);
    tw.addFunc("reset", &Tween::reset);
    tw.addFunc("isRunning", &Tween::isRunning);
    tw.addFunc("isPaused", &Tween::isPaused);
    tw.addFunc("isFinished", &Tween::isFinished);
    tw.addFunc("isStopped", &Tween::isStopped);
    tw.addFunc("isDelayed", &Tween::isDelayed);
    tw.addFunc("isActive", &Tween::isActive);
    tw.addFunc("getElapsed", &Tween::getElapsed);
    tw.addFunc("getProgress", &Tween::getProgress);
    tw.addFunc("getEasedProgress", &Tween::getEasedProgress);
    tw.addFunc("update", &Tween::update);
    tw.addFunc("evaluate", &Tween::evaluate);
    tw.addFunc("getPropertyCount", &Tween::getPropertyCount);
    tw.addFunc("getPropertyName", &Tween::getPropertyName);

    auto sk = table.addClass<AnimSkeleton>(
        "AnimSkeleton", std::function<AnimSkeleton *()>([]() -> AnimSkeleton * { return nullptr; }),
        true);
    sk.addFunc("addBone", &AnimSkeleton::addBone);
    sk.addFunc("getBoneCount", &AnimSkeleton::getBoneCount);
    sk.addFunc("getBoneName", &AnimSkeleton::getBoneName);
    sk.addFunc("findBone", &AnimSkeleton::findBone);
    sk.addFunc("getParent", &AnimSkeleton::getParent);
    sk.addFunc("setBindPosition", &AnimSkeleton::setBindPosition);
    sk.addFunc("setBindRotation", &AnimSkeleton::setBindRotation);
    sk.addFunc("setBindScale", &AnimSkeleton::setBindScale);
    sk.addFunc("getBindPositionX", &AnimSkeleton::getBindPositionX);
    sk.addFunc("getBindPositionY", &AnimSkeleton::getBindPositionY);
    sk.addFunc("getBindPositionZ", &AnimSkeleton::getBindPositionZ);
    sk.addFunc("getBindRotationX", &AnimSkeleton::getBindRotationX);
    sk.addFunc("getBindRotationY", &AnimSkeleton::getBindRotationY);
    sk.addFunc("getBindRotationZ", &AnimSkeleton::getBindRotationZ);
    sk.addFunc("getBindRotationW", &AnimSkeleton::getBindRotationW);
    sk.addFunc("getBindScaleX", &AnimSkeleton::getBindScaleX);
    sk.addFunc("getBindScaleY", &AnimSkeleton::getBindScaleY);
    sk.addFunc("getBindScaleZ", &AnimSkeleton::getBindScaleZ);
    sk.addFunc("applyBindPose", &AnimSkeleton::applyBindPose);

    auto clip = table.addClass<AnimClip>(
        "AnimClip", std::function<AnimClip *()>([]() -> AnimClip * { return nullptr; }), true);
    clip.addFunc("setName", &AnimClip::setName);
    clip.addFunc("getName", &AnimClip::getName);
    clip.addFunc("setDuration", &AnimClip::setDuration);
    clip.addFunc("getDuration", &AnimClip::getDuration);
    clip.addFunc("setLoop", &AnimClip::setLoop);
    clip.addFunc("getLoop", &AnimClip::getLoop);
    clip.addFunc("setSampleRate", &AnimClip::setSampleRate);
    clip.addFunc("getSampleRate", &AnimClip::getSampleRate);
    clip.addFunc("addPositionKey", &AnimClip::addPositionKey);
    clip.addFunc("addRotationKey", &AnimClip::addRotationKey);
    clip.addFunc("addScaleKey", &AnimClip::addScaleKey);
    clip.addFunc("getPositionKeyCount", &AnimClip::getPositionKeyCount);
    clip.addFunc("getRotationKeyCount", &AnimClip::getRotationKeyCount);
    clip.addFunc("getScaleKeyCount", &AnimClip::getScaleKeyCount);
    clip.addFunc("applyPlanarRootMotion", &AnimClip::applyPlanarRootMotion);
    clip.addFunc("sample", &AnimClip::sample);
    clip.addFunc("wrapTime", &AnimClip::wrapTime);

    auto pose = table.addClass<AnimPose>(
        "AnimPose", std::function<AnimPose *()>([]() -> AnimPose * { return nullptr; }), true);
    pose.addFunc("resize", &AnimPose::resize);
    pose.addFunc("getBoneCount", &AnimPose::getBoneCount);
    pose.addFunc("copyFrom", &AnimPose::copyFrom);
    pose.addFunc("blendFrom", &AnimPose::blendFrom);
    pose.addFunc("setLocalPosition", &AnimPose::setLocalPosition);
    pose.addFunc("setLocalRotation", &AnimPose::setLocalRotation);
    pose.addFunc("setLocalScale", &AnimPose::setLocalScale);
    pose.addFunc("getLocalPositionX", &AnimPose::getLocalPositionX);
    pose.addFunc("getLocalPositionY", &AnimPose::getLocalPositionY);
    pose.addFunc("getLocalPositionZ", &AnimPose::getLocalPositionZ);
    pose.addFunc("getLocalRotationX", &AnimPose::getLocalRotationX);
    pose.addFunc("getLocalRotationY", &AnimPose::getLocalRotationY);
    pose.addFunc("getLocalRotationZ", &AnimPose::getLocalRotationZ);
    pose.addFunc("getLocalRotationW", &AnimPose::getLocalRotationW);
    pose.addFunc("getLocalScaleX", &AnimPose::getLocalScaleX);
    pose.addFunc("getLocalScaleY", &AnimPose::getLocalScaleY);
    pose.addFunc("getLocalScaleZ", &AnimPose::getLocalScaleZ);
    pose.addFunc("computeWorld", &AnimPose::computeWorld);
    pose.addFunc("getWorldPositionX", &AnimPose::getWorldPositionX);
    pose.addFunc("getWorldPositionY", &AnimPose::getWorldPositionY);
    pose.addFunc("getWorldPositionZ", &AnimPose::getWorldPositionZ);
    pose.addFunc("getWorldRotationX", &AnimPose::getWorldRotationX);
    pose.addFunc("getWorldRotationY", &AnimPose::getWorldRotationY);
    pose.addFunc("getWorldRotationZ", &AnimPose::getWorldRotationZ);
    pose.addFunc("getWorldRotationW", &AnimPose::getWorldRotationW);
    pose.addFunc("getWorldMatrixElement", &AnimPose::getWorldMatrixElement);

    auto skin = table.addClass<AnimSkin>(
        "AnimSkin", std::function<AnimSkin *()>([]() -> AnimSkin * { return nullptr; }), true);
    skin.addFunc("getVertexCount", &AnimSkin::getVertexCount);
    skin.addFunc("getBoneCount", &AnimSkin::getBoneCount);
    skin.addFunc("getInfluenceCount", &AnimSkin::getInfluenceCount);
    skin.addFunc("getSkeletonBone", &AnimSkin::getSkeletonBone);
    skin.addFunc("getSkinBoneName", &AnimSkin::getSkinBoneName);
    skin.addFunc("getInverseBindElement", &AnimSkin::getInverseBindElement);
    skin.addFunc("getBindPositionX", &AnimSkin::getBindPositionX);
    skin.addFunc("getBindPositionY", &AnimSkin::getBindPositionY);
    skin.addFunc("getBindPositionZ", &AnimSkin::getBindPositionZ);
    skin.addFunc("getVertexBone", &AnimSkin::getVertexBone);
    skin.addFunc("getVertexWeight", &AnimSkin::getVertexWeight);

    auto player = table.addClass<AnimPlayer>(
        "AnimPlayer", std::function<AnimPlayer *()>([]() -> AnimPlayer * { return nullptr; }),
        true);
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
    player.addFunc("update", &AnimPlayer::update);

    auto sm = table.addClass<AnimStateMachine>(
        "AnimStateMachine",
        std::function<AnimStateMachine *()>([]() -> AnimStateMachine * { return nullptr; }), true);
    sm.addFunc("addState", &AnimStateMachine::addState);
    sm.addFunc("setEntry", &AnimStateMachine::setEntry);
    sm.addFunc("hasState", &AnimStateMachine::hasState);
    sm.addFunc("getCurrentState", &AnimStateMachine::getCurrentState);
    sm.addFunc("getStateCount", &AnimStateMachine::getStateCount);
    sm.addFunc("addTransition", &AnimStateMachine::addTransition);
    sm.addFunc("addFloatCondition", &AnimStateMachine::addFloatCondition);
    sm.addFunc("addBoolCondition", &AnimStateMachine::addBoolCondition);
    sm.addFunc("addTriggerCondition", &AnimStateMachine::addTriggerCondition);
    sm.addFunc("setExitTime", &AnimStateMachine::setExitTime);
    sm.addFunc("setHasExitTime", &AnimStateMachine::setHasExitTime);
    sm.addFunc("setFloat", &AnimStateMachine::setFloat);
    sm.addFunc("getFloat", &AnimStateMachine::getFloat);
    sm.addFunc("setBool", &AnimStateMachine::setBool);
    sm.addFunc("getBool", &AnimStateMachine::getBool);
    sm.addFunc("setTrigger", &AnimStateMachine::setTrigger);
    sm.addFunc("resetTrigger", &AnimStateMachine::resetTrigger);
    sm.addFunc("getPose", &AnimStateMachine::getPose);
    sm.addFunc("getStateTime", &AnimStateMachine::getStateTime);
    sm.addFunc("isBlending", &AnimStateMachine::isBlending);
    sm.addFunc("update", &AnimStateMachine::update);

    auto db = table.addClass<MotionDatabase>(
        "MotionDatabase",
        std::function<MotionDatabase *()>([]() -> MotionDatabase * { return nullptr; }), true);
    db.addFunc("addFeatureBone", &MotionDatabase::addFeatureBone);
    db.addFunc("addFeatureBoneByName", &MotionDatabase::addFeatureBoneByName);
    db.addFunc("setRootBone", &MotionDatabase::setRootBone);
    db.addFunc("getRootBone", &MotionDatabase::getRootBone);
    db.addFunc("setRootBoneByName", &MotionDatabase::setRootBoneByName);
    db.addFunc("addClip", &MotionDatabase::addClip);
    db.addFunc("getClipCount", &MotionDatabase::getClipCount);
    db.addFunc("bake", &MotionDatabase::bake);
    db.addFunc("isBaked", &MotionDatabase::isBaked);
    db.addFunc("getFrameCount", &MotionDatabase::getFrameCount);
    db.addFunc("getFeatureSize", &MotionDatabase::getFeatureSize);
    db.addFunc("getFrameTime", &MotionDatabase::getFrameTime);
    db.addFunc("getFrameClipIndex", &MotionDatabase::getFrameClipIndex);
    db.addFunc("getFeatureBoneCount", &MotionDatabase::getFeatureBoneCount);
    db.addFunc("getFeatureBone", &MotionDatabase::getFeatureBone);

    auto mm = table.addClass<MotionMatcher>(
        "MotionMatcher",
        std::function<MotionMatcher *()>([]() -> MotionMatcher * { return nullptr; }), true);
    mm.addFunc("setDesiredVelocity", &MotionMatcher::setDesiredVelocity);
    mm.addFunc("getDesiredVelocityX", &MotionMatcher::getDesiredVelocityX);
    mm.addFunc("getDesiredVelocityZ", &MotionMatcher::getDesiredVelocityZ);
    mm.addFunc("setDesiredYaw", &MotionMatcher::setDesiredYaw);
    mm.addFunc("getDesiredYaw", &MotionMatcher::getDesiredYaw);
    mm.addFunc("setSearchInterval", &MotionMatcher::setSearchInterval);
    mm.addFunc("getSearchInterval", &MotionMatcher::getSearchInterval);
    mm.addFunc("setBlendTime", &MotionMatcher::setBlendTime);
    mm.addFunc("getBlendTime", &MotionMatcher::getBlendTime);
    mm.addFunc("setTrajectoryWeight", &MotionMatcher::setTrajectoryWeight);
    mm.addFunc("getTrajectoryWeight", &MotionMatcher::getTrajectoryWeight);
    mm.addFunc("setPoseWeight", &MotionMatcher::setPoseWeight);
    mm.addFunc("getPoseWeight", &MotionMatcher::getPoseWeight);
    mm.addFunc("setVelocityWeight", &MotionMatcher::setVelocityWeight);
    mm.addFunc("getVelocityWeight", &MotionMatcher::getVelocityWeight);
    mm.addFunc("setIgnoreRadius", &MotionMatcher::setIgnoreRadius);
    mm.addFunc("getIgnoreRadius", &MotionMatcher::getIgnoreRadius);
    mm.addFunc("getMatchedFrame", &MotionMatcher::getMatchedFrame);
    mm.addFunc("getMatchedClipIndex", &MotionMatcher::getMatchedClipIndex);
    mm.addFunc("getMatchedTime", &MotionMatcher::getMatchedTime);
    mm.addFunc("getLastSearchCost", &MotionMatcher::getLastSearchCost);
    mm.addFunc("getPose", &MotionMatcher::getPose);
    mm.addFunc("search", &MotionMatcher::search);
    mm.addFunc("update", &MotionMatcher::update);

    auto ca = table.addClass<ControlAnim>(
        "ControlAnim", std::function<ControlAnim *()>([]() -> ControlAnim * { return nullptr; }),
        true);
    ca.addFunc("setFrequency", &ControlAnim::setFrequency);
    ca.addFunc("getFrequency", &ControlAnim::getFrequency);
    ca.addFunc("setDamping", &ControlAnim::setDamping);
    ca.addFunc("getDamping", &ControlAnim::getDamping);
    ca.addFunc("setResponse", &ControlAnim::setResponse);
    ca.addFunc("getResponse", &ControlAnim::getResponse);
    ca.addFunc("setIntegrator", &ControlAnim::setIntegrator);
    ca.addFunc("getIntegrator", &ControlAnim::getIntegrator);
    ca.addFunc("set", &ControlAnim::set);
    ca.addFunc("setTarget", &ControlAnim::setTarget);
    ca.addFunc("setTargetVelocity", &ControlAnim::setTargetVelocity);
    ca.addFunc("impulse", &ControlAnim::impulse);
    ca.addFunc("has", &ControlAnim::has);
    ca.addFunc("get", &ControlAnim::get);
    ca.addFunc("getVelocity", &ControlAnim::getVelocity);
    ca.addFunc("getTarget", &ControlAnim::getTarget);
    ca.addFunc("clear", &ControlAnim::clear);
    ca.addFunc("remove", &ControlAnim::remove);
    ca.addFunc("getPropertyCount", &ControlAnim::getPropertyCount);
    ca.addFunc("getPropertyName", &ControlAnim::getPropertyName);
    ca.addFunc("update", &ControlAnim::update);

    auto cp = table.addClass<ControlPose>(
        "ControlPose", std::function<ControlPose *()>([]() -> ControlPose * { return nullptr; }),
        true);
    cp.addFunc("setFrequency", &ControlPose::setFrequency);
    cp.addFunc("getFrequency", &ControlPose::getFrequency);
    cp.addFunc("setDamping", &ControlPose::setDamping);
    cp.addFunc("getDamping", &ControlPose::getDamping);
    cp.addFunc("setResponse", &ControlPose::setResponse);
    cp.addFunc("getResponse", &ControlPose::getResponse);
    cp.addFunc("setIntegrator", &ControlPose::setIntegrator);
    cp.addFunc("getIntegrator", &ControlPose::getIntegrator);
    cp.addFunc("setBoneWeight", &ControlPose::setBoneWeight);
    cp.addFunc("getBoneWeight", &ControlPose::getBoneWeight);
    cp.addFunc("setTargetPose", &ControlPose::setTargetPose);
    cp.addFunc("snapToTarget", &ControlPose::snapToTarget);
    cp.addFunc("getPose", &ControlPose::getPose);
    cp.addFunc("getTargetPose", &ControlPose::getTargetPose);
    cp.addFunc("update", &ControlPose::update);

    auto trail = table.addClass<AnimTrail>(
        "AnimTrail", std::function<AnimTrail *()>([]() -> AnimTrail * { return nullptr; }), true);
    trail.addFunc("setCapacity", &AnimTrail::setCapacity);
    trail.addFunc("getCapacity", &AnimTrail::getCapacity);
    trail.addFunc("setDuration", &AnimTrail::setDuration);
    trail.addFunc("getDuration", &AnimTrail::getDuration);
    trail.addFunc("setMinDistance", &AnimTrail::setMinDistance);
    trail.addFunc("getMinDistance", &AnimTrail::getMinDistance);
    trail.addFunc("setWidth", &AnimTrail::setWidth);
    trail.addFunc("getWidth", &AnimTrail::getWidth);
    trail.addFunc("setColor", &AnimTrail::setColor);
    trail.addFunc("getColorR", &AnimTrail::getColorR);
    trail.addFunc("getColorG", &AnimTrail::getColorG);
    trail.addFunc("getColorB", &AnimTrail::getColorB);
    trail.addFunc("getColorA", &AnimTrail::getColorA);
    trail.addFunc("setFade", &AnimTrail::setFade);
    trail.addFunc("getFade", &AnimTrail::getFade);
    trail.addFunc("setStyle", &AnimTrail::setStyle);
    trail.addFunc("getStyle", &AnimTrail::getStyle);
    trail.addFunc("setDrawScale", &AnimTrail::setDrawScale);
    trail.addFunc("getDrawScaleX", &AnimTrail::getDrawScaleX);
    trail.addFunc("getDrawScaleY", &AnimTrail::getDrawScaleY);
    trail.addFunc("setDrawOffset", &AnimTrail::setDrawOffset);
    trail.addFunc("getDrawOffsetX", &AnimTrail::getDrawOffsetX);
    trail.addFunc("getDrawOffsetY", &AnimTrail::getDrawOffsetY);
    trail.addFunc("addPoint", &AnimTrail::addPoint);
    trail.addFunc("addPoint3", &AnimTrail::addPoint3);
    trail.addFunc("sampleBone", &AnimTrail::sampleBone);
    trail.addFunc("sampleBoneOffset", &AnimTrail::sampleBoneOffset);
    trail.addFunc("clear", &AnimTrail::clear);
    trail.addFunc("update", &AnimTrail::update);
    trail.addFunc("getPointCount", &AnimTrail::getPointCount);
    trail.addFunc("getPointX", &AnimTrail::getPointX);
    trail.addFunc("getPointY", &AnimTrail::getPointY);
    trail.addFunc("getPointZ", &AnimTrail::getPointZ);
    trail.addFunc("getPointAge", &AnimTrail::getPointAge);
    trail.addFunc("getPointAlpha", &AnimTrail::getPointAlpha);
    trail.addFunc("draw", &AnimTrail::draw);
}

void Animation::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Animation::getName);
    cls.addFunc("newTween", &Animation::newTween);
    cls.addFunc("newSkeleton", &Animation::newSkeleton);
    cls.addFunc("newClip", &Animation::newClip);
    cls.addFunc("newPose", &Animation::newPose);
    cls.addFunc("newPlayer", &Animation::newPlayer);
    cls.addFunc("newStateMachine", &Animation::newStateMachine);
    cls.addFunc("newMotionDatabase", &Animation::newMotionDatabase);
    cls.addFunc("newMotionMatcher", &Animation::newMotionMatcher);
    cls.addFunc("newControlAnim", &Animation::newControlAnim);
    cls.addFunc("newControlPose", &Animation::newControlPose);
    cls.addFunc("newSkeletonFromModel", &Animation::newSkeletonFromModel);
    cls.addFunc("newClipFromModel", &Animation::newClipFromModel);
    cls.addFunc("newSkeletonFromEvaFile", &Animation::newSkeletonFromEvaFile);
    cls.addFunc("newClipFromEvaFile", &Animation::newClipFromEvaFile);
    cls.addFunc("newSkinFromModel", &Animation::newSkinFromModel);
    cls.addFunc("newTrail", &Animation::newTrail);
    cls.addFunc("update", &Animation::update);
    cls.addFunc("getTweenCount", &Animation::getTweenCount);
    cls.addFunc("getActiveCount", &Animation::getActiveCount);
    cls.addFunc("clearFinished", &Animation::clearFinished);
    cls.addFunc("clearAll", &Animation::clearAll);
}

}  // namespace eve::animation
