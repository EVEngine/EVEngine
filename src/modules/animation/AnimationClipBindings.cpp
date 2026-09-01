#include "animation/AnimationBindings.h"

#include "animation/AnimClip.h"

#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::animation {

void exposeAnimClipBindings(ssq::Table& table) {
    auto retarget = table.addClass<AnimRetargetProfile>(
        "AnimRetargetProfile", std::function<AnimRetargetProfile*()>([]() { return new AnimRetargetProfile(); }), true);
    retarget.addFunc("addBoneMapping", &AnimRetargetProfile::addBoneMapping);
    retarget.addFunc("clearBoneMappings", &AnimRetargetProfile::clearBoneMappings);
    retarget.addFunc("setNormalizedNameMatching", &AnimRetargetProfile::setNormalizedNameMatching);
    retarget.addFunc("getNormalizedNameMatching", &AnimRetargetProfile::getNormalizedNameMatching);
    retarget.addFunc("setRootBones", &AnimRetargetProfile::setRootBones);
    retarget.addFunc("setAutoRootScale", &AnimRetargetProfile::setAutoRootScale);
    retarget.addFunc("getAutoRootScale", &AnimRetargetProfile::getAutoRootScale);
    retarget.addFunc("setRootTranslationScale", &AnimRetargetProfile::setRootTranslationScale);
    retarget.addFunc("getRootHorizontalScale", &AnimRetargetProfile::getRootHorizontalScale);
    retarget.addFunc("getRootVerticalScale", &AnimRetargetProfile::getRootVerticalScale);
    retarget.addFunc("setUseSkeletonSpaceRotation", &AnimRetargetProfile::setUseSkeletonSpaceRotation);
    retarget.addFunc("getUseSkeletonSpaceRotation", &AnimRetargetProfile::getUseSkeletonSpaceRotation);
    retarget.addFunc("getMatchedBoneCount", &AnimRetargetProfile::getMatchedBoneCount);
    retarget.addFunc("getUnmatchedBoneCount", &AnimRetargetProfile::getUnmatchedBoneCount);
    retarget.addFunc("getUnmatchedTargetBone", &AnimRetargetProfile::getUnmatchedTargetBone);

    auto clip = table.addClass<AnimClip>(
        "AnimClip", std::function<AnimClip*()>([]() -> AnimClip* { return nullptr; }), true);
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
    clip.addFunc("setPositionKey", &AnimClip::setPositionKey);
    clip.addFunc("setRotationKey", &AnimClip::setRotationKey);
    clip.addFunc("setScaleKey", &AnimClip::setScaleKey);
    clip.addFunc("removePositionKey", &AnimClip::removePositionKey);
    clip.addFunc("removeRotationKey", &AnimClip::removeRotationKey);
    clip.addFunc("removeScaleKey", &AnimClip::removeScaleKey);
    clip.addFunc("clearTrack", &AnimClip::clearTrack);
    clip.addFunc("getTrackCount", &AnimClip::getTrackCount);
    clip.addFunc("addEvent", &AnimClip::addEvent);
    clip.addFunc("setEvent", &AnimClip::setEvent);
    clip.addFunc("removeEvent", &AnimClip::removeEvent);
    clip.addFunc("getEventCount", &AnimClip::getEventCount);
    clip.addFunc("getEventTime", &AnimClip::getEventTime);
    clip.addFunc("getEventName", &AnimClip::getEventName);
    clip.addFunc("getEventPayload", &AnimClip::getEventPayload);
    clip.addFunc("addSyncMarker", &AnimClip::addSyncMarker);
    clip.addFunc("setSyncMarker", &AnimClip::setSyncMarker);
    clip.addFunc("removeSyncMarker", &AnimClip::removeSyncMarker);
    clip.addFunc("getSyncMarkerCount", &AnimClip::getSyncMarkerCount);
    clip.addFunc("getSyncMarkerTime", &AnimClip::getSyncMarkerTime);
    clip.addFunc("getSyncMarkerName", &AnimClip::getSyncMarkerName);
    clip.addFunc("hasCompatibleSyncMarkers", &AnimClip::hasCompatibleSyncMarkers);
    clip.addFunc("mapSyncTimeTo", &AnimClip::mapSyncTimeTo);
    clip.addFunc("getPositionKeyCount", &AnimClip::getPositionKeyCount);
    clip.addFunc("getPositionKeyTime", &AnimClip::getPositionKeyTime);
    clip.addFunc("getPositionKeyX", &AnimClip::getPositionKeyX);
    clip.addFunc("getPositionKeyY", &AnimClip::getPositionKeyY);
    clip.addFunc("getPositionKeyZ", &AnimClip::getPositionKeyZ);
    clip.addFunc("getRotationKeyCount", &AnimClip::getRotationKeyCount);
    clip.addFunc("getRotationKeyTime", &AnimClip::getRotationKeyTime);
    clip.addFunc("getRotationKeyX", &AnimClip::getRotationKeyX);
    clip.addFunc("getRotationKeyY", &AnimClip::getRotationKeyY);
    clip.addFunc("getRotationKeyZ", &AnimClip::getRotationKeyZ);
    clip.addFunc("getRotationKeyW", &AnimClip::getRotationKeyW);
    clip.addFunc("getScaleKeyCount", &AnimClip::getScaleKeyCount);
    clip.addFunc("getScaleKeyTime", &AnimClip::getScaleKeyTime);
    clip.addFunc("getScaleKeyX", &AnimClip::getScaleKeyX);
    clip.addFunc("getScaleKeyY", &AnimClip::getScaleKeyY);
    clip.addFunc("getScaleKeyZ", &AnimClip::getScaleKeyZ);
    clip.addFunc("applyPlanarRootMotion", &AnimClip::applyPlanarRootMotion);
    clip.addFunc("compress", &AnimClip::compress);
    clip.addFunc("retarget", &AnimClip::retarget);
    clip.addFunc("retargetWithProfile", &AnimClip::retargetWithProfile);
    clip.addFunc("sample", &AnimClip::sample);
    clip.addFunc("sampleLod", &AnimClip::sampleLod);
    clip.addFunc("wrapTime", &AnimClip::wrapTime);
}

}  // namespace eve::animation
