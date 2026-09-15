#include "animation/AnimationBindings.h"

#include "animation/AnimClip.h"
#include "animation/AnimClipBinary.h"
#include "animation/AnimSkeleton.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>

namespace eve::animation {

void exposeAnimClipBindings(ssq::Table& table) {
    exposeAnimCurveLibraryBindings(table);
    table.addFunc("loadAnimationTrackBatch", [vm = table.getHandle()](ssq::Array clips, ssq::Array paths,
                                                                      AnimSkeleton* skeleton, int workers) {
        ssq::Table result(vm);
        try {
            if (!skeleton || clips.size() != paths.size() || clips.size() > 64)
                throw std::runtime_error("invalid animation batch arguments");
            auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
            if (!fs) throw std::runtime_error("filesystem unavailable");
            std::vector<eve::ref<eve::filesystem::FileData>> buffers;
            std::vector<AnimationTrackInput>                 inputs;
            std::size_t                                      total = 0;
            for (std::size_t i = 0; i < clips.size(); ++i) {
                auto* destination = clips.get<AnimClip*>(i);
                if (!destination) throw std::runtime_error("missing animation batch destination");
                auto* raw = fs->read(paths.get<std::string>(i));
                if (!raw) throw std::runtime_error("animation track file unavailable");
                eve::ref<eve::filesystem::FileData> data(raw);
                if (data->getSize() > 256 * 1024 * 1024 - total)
                    throw std::runtime_error("animation batch exceeds 256 MiB limit");
                total += data->getSize();
                inputs.push_back({*destination, {static_cast<const std::byte*>(data->getData()), data->getSize()}});
                buffers.push_back(data);
            }
            auto loaded = loadAnimationTrackBatch(inputs, *skeleton, workers);
            result.set("ok", loaded.ok());
            result.set("message", loaded.status().describe());
            result.set("workers", loaded.ok() ? loaded.value() : 0);
        } catch (const std::exception& error) {
            result.set("ok", false);
            result.set("message", std::string(error.what()));
            result.set("workers", 0);
        }
        return result;
    });
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
    retarget.addFunc("setSkinnedInteractionPreserve", &AnimRetargetProfile::setSkinnedInteractionPreserve);
    retarget.addFunc("getSkinnedInteractionPreserve", &AnimRetargetProfile::getSkinnedInteractionPreserve);
    retarget.addFunc("setInteractionContactThreshold", &AnimRetargetProfile::setInteractionContactThreshold);
    retarget.addFunc("getInteractionContactThreshold", &AnimRetargetProfile::getInteractionContactThreshold);
    retarget.addFunc("setInteractionCorrectionWeight", &AnimRetargetProfile::setInteractionCorrectionWeight);
    retarget.addFunc("getInteractionCorrectionWeight", &AnimRetargetProfile::getInteractionCorrectionWeight);
    retarget.addFunc("addInteractionIkChain", &AnimRetargetProfile::addInteractionIkChain);
    retarget.addFunc("clearInteractionIkChains", &AnimRetargetProfile::clearInteractionIkChains);
    retarget.addFunc("getInteractionCorrectionCount", &AnimRetargetProfile::getInteractionCorrectionCount);
    retarget.addFunc("setNeuralModelPath", &AnimRetargetProfile::setNeuralModelPath);
    retarget.addFunc("getNeuralModelPath", &AnimRetargetProfile::getNeuralModelPath);
    retarget.addFunc("setNeuralRetargetEnabled", &AnimRetargetProfile::setNeuralRetargetEnabled);
    retarget.addFunc("getNeuralRetargetEnabled", &AnimRetargetProfile::getNeuralRetargetEnabled);
    retarget.addFunc("setNeuralBackend", &AnimRetargetProfile::setNeuralBackend);
    retarget.addFunc("getNeuralBackend", &AnimRetargetProfile::getNeuralBackend);
    retarget.addFunc("getNeuralInferenceCount", &AnimRetargetProfile::getNeuralInferenceCount);
    retarget.addFunc("getMatchedBoneCount", &AnimRetargetProfile::getMatchedBoneCount);
    retarget.addFunc("getUnmatchedBoneCount", &AnimRetargetProfile::getUnmatchedBoneCount);
    retarget.addFunc("getUnmatchedTargetBone", &AnimRetargetProfile::getUnmatchedTargetBone);

    auto clip = table.addClass<AnimClip>("AnimClip", std::function<AnimClip*()>([]() { return new AnimClip(); }), true);
    clip.addFunc("loadBinary", [vm = table.getHandle()](AnimClip* self, std::string path, AnimSkeleton* skeleton) {
        ssq::Table result(vm);
        try {
            if (!self || !skeleton) throw std::runtime_error("missing clip or skeleton");
            auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
            if (!fs) throw std::runtime_error("filesystem unavailable");
            auto* raw = fs->read(path);
            if (!raw) throw std::runtime_error("animation track file unavailable");
            eve::ref<eve::filesystem::FileData> data(raw);
            auto loaded = loadAnimationTracks(*self, {static_cast<const std::byte*>(data->getData()), data->getSize()},
                                              *skeleton);
            result.set("ok", loaded.ok());
            result.set("message", loaded.status().describe());
        } catch (const std::exception& error) {
            result.set("ok", false);
            result.set("message", std::string(error.what()));
        }
        return result;
    });
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
