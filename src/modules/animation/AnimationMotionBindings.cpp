#include <array>
#include <cstdint>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "animation/AnimationBindings.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "common/SquirrelBindContext.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve::animation {
namespace {

constexpr const char* kSource = "animation.bindings";


}  // namespace

// A provider read hands over a freshly allocated FileData that this call owns.
using eve::script::Owned;

void exposeMotionMatcherBindings(ssq::Table& table) {
    const script::BindContext bind{table.getHandle(), kSource};
    exposeAnimInertializerBindings(table);
    exposeOrientationWarpingBindings(table);
    auto db = table.addClass<MotionDatabase>(
        "MotionDatabase", std::function<MotionDatabase*()>([]() -> MotionDatabase* { return nullptr; }), true);
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
    db.addFunc("setLocomotionFeatures", [bind](MotionDatabase* self, int left, int right, int pelvis) {
        return script::projectResult(bind.vm(), self->setLocomotionFeatures(left, right, pelvis));
    });
    db.addFunc("hasLocomotionFeatures", &MotionDatabase::hasLocomotionFeatures);
    db.addFunc("hasFeatureLayout", &MotionDatabase::hasFeatureLayout);
    db.addFunc("loadFeatureCurves", [bind](MotionDatabase* self, std::string path, ssq::Array sources) {
        try {
            if (sources.size() != static_cast<std::size_t>(self->getClipCount()))
                throw std::runtime_error("one curve source per database clip is required");
            std::vector<std::string> names;
            for (std::size_t i = 0; i < sources.size(); ++i) names.push_back(sources.get<std::string>(i));
            auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
            if (!fs) throw std::runtime_error("filesystem unavailable");
            auto* raw = fs->read(path);
            if (!raw) throw std::runtime_error("feature curve file unavailable");
            Owned<eve::filesystem::FileData> data(raw);
            return script::projectResult(
                bind.vm(),
                self->setFeatureCurves({static_cast<const std::byte*>(data->getData()), data->getSize()}, names));
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    db.addFunc("setFeatureLayout", [bind](MotionDatabase* self, int rate, ssq::Array entries, float lengthScale) {
        try {
            if (entries.size() == 0 || entries.size() > 256)
                throw std::runtime_error("feature layout requires 1..256 channels");
            MotionFeatureLayout layout;
            layout.sampleRate               = rate;
            layout.normalizationLengthScale = lengthScale;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 12 && item.size() != 13)
                    throw std::runtime_error("feature channel requires twelve fields plus an optional curve name");
                MotionFeatureChannel c;
                const auto           kind = item.get<std::string>(0), source = item.get<std::string>(1),
                           query = item.get<std::string>(2);
                if (kind == "position")
                    c.kind = MotionFeatureKind::Position;
                else if (kind == "velocity")
                    c.kind = MotionFeatureKind::Velocity;
                else if (kind == "heading")
                    c.kind = MotionFeatureKind::Heading;
                else if (kind == "curve")
                    c.kind = MotionFeatureKind::Curve;
                else
                    throw std::runtime_error("unsupported feature operation");
                if (source == "pose")
                    c.source = MotionFeatureSource::Pose;
                else if (source == "trajectory")
                    c.source = MotionFeatureSource::Trajectory;
                else
                    throw std::runtime_error("unsupported feature source");
                if (query == "USE_CHARACTER_POSE")
                    c.query = MotionFeatureQuery::Character;
                else if (query == "USE_CONTINUING_POSE")
                    c.query = MotionFeatureQuery::Continuing;
                else
                    throw std::runtime_error("unsupported feature query policy");
                c.bone         = item.get<int>(3);
                c.origin       = item.get<int>(4);
                const int axes = item.get<int>(5);
                if (axes < 1 || axes > 7) throw std::runtime_error("feature axes must be a nonempty XYZ mask");
                c.axes                   = static_cast<std::uint8_t>(axes);
                c.headingAxis            = item.get<int>(6);
                c.sampleTime             = item.get<float>(7);
                c.weight                 = item.get<float>(8);
                c.characterSpaceVelocity = item.get<bool>(9);
                c.normalizeVelocity      = item.get<bool>(10);
                c.normalizationGroup     = item.get<std::string>(11);
                if (item.size() == 13) c.curve = item.get<std::string>(12);
                layout.channels.push_back(std::move(c));
            }
            return script::projectResult(bind.vm(), self->setFeatureLayout(layout));
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    db.addFunc("setFeatureNormalizationRanges", [bind](MotionDatabase* self, ssq::Array entries) {
        try {
            if (entries.size() > 100000) throw std::runtime_error("too many normalization ranges");
            std::vector<MotionNormalizationRange> ranges;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 3) throw std::runtime_error("normalization range requires clip,start,end");
                ranges.push_back({item.get<int>(0), item.get<float>(1), item.get<float>(2)});
            }
            return script::projectResult(bind.vm(), self->setFeatureNormalizationRanges(ranges),
                                         [](int count) { return Value(count); });
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    auto mm = table.addClass<MotionMatcher>(
        "MotionMatcher", std::function<MotionMatcher*()>([]() -> MotionMatcher* { return nullptr; }), true);
    mm.addFunc("setDesiredVelocity", &MotionMatcher::setDesiredVelocity);
    mm.addFunc("getDesiredVelocityX", &MotionMatcher::getDesiredVelocityX);
    mm.addFunc("getDesiredVelocityZ", &MotionMatcher::getDesiredVelocityZ);
    mm.addFunc("setDesiredYaw", &MotionMatcher::setDesiredYaw);
    mm.addFunc("getDesiredYaw", &MotionMatcher::getDesiredYaw);
    mm.addFunc("setSearchInterval", &MotionMatcher::setSearchInterval);
    mm.addFunc("getSearchInterval", &MotionMatcher::getSearchInterval);
    mm.addFunc("setBlendTime", &MotionMatcher::setBlendTime);
    mm.addFunc("getBlendTime", &MotionMatcher::getBlendTime);
    mm.addFunc("setPlayRateRange", [bind](MotionMatcher* self, float minimum, float maximum) {
        return script::projectResult(bind.vm(), self->setPlayRateRange(minimum, maximum));
    });
    mm.addFunc("getPlayRateMinimum", &MotionMatcher::getPlayRateMinimum);
    mm.addFunc("getPlayRateMaximum", &MotionMatcher::getPlayRateMaximum);
    mm.addFunc("getPlayRate", &MotionMatcher::getPlayRate);
    mm.addFunc("setTrajectoryWeight", &MotionMatcher::setTrajectoryWeight);
    mm.addFunc("getTrajectoryWeight", &MotionMatcher::getTrajectoryWeight);
    mm.addFunc("setPoseWeight", &MotionMatcher::setPoseWeight);
    mm.addFunc("getPoseWeight", &MotionMatcher::getPoseWeight);
    mm.addFunc("setVelocityWeight", &MotionMatcher::setVelocityWeight);
    mm.addFunc("getVelocityWeight", &MotionMatcher::getVelocityWeight);
    mm.addFunc("setIgnoreRadius", &MotionMatcher::setIgnoreRadius);
    mm.addFunc("getIgnoreRadius", &MotionMatcher::getIgnoreRadius);
    mm.addFunc("setPoseReselectHistory", [bind](MotionMatcher* self, float seconds) {
        return script::projectResult(bind.vm(), self->setPoseReselectHistory(seconds));
    });
    mm.addFunc("getPoseReselectHistory", &MotionMatcher::getPoseReselectHistory);
    mm.addFunc("getMatchedFrame", &MotionMatcher::getMatchedFrame);
    mm.addFunc("getMatchedClipIndex", &MotionMatcher::getMatchedClipIndex);
    mm.addFunc("getMatchedTime", &MotionMatcher::getMatchedTime);
    mm.addFunc("getLastSearchCost", &MotionMatcher::getLastSearchCost);
    mm.addFunc("getPose", &MotionMatcher::getPose);
    mm.addFunc("search", &MotionMatcher::search);
    mm.addFunc("update", &MotionMatcher::update);
    mm.addFunc("setFeatureQuery", [bind](MotionMatcher* self, AnimPose* current, AnimPose* previous, float dt,
                                         ssq::Array entries, ssq::Array curveEntries) {
        try {
            if (!current || !previous || entries.size() > 256)
                throw std::runtime_error("feature query requires two poses and bounded trajectory samples");
            std::vector<MotionFeatureTrajectorySample> samples;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 8) throw std::runtime_error("feature sample requires time,x,y,z,vx,vy,vz,yaw");
                samples.push_back({item.get<float>(0), item.get<float>(1), item.get<float>(2), item.get<float>(3),
                                   item.get<float>(4), item.get<float>(5), item.get<float>(6), item.get<float>(7)});
            }
            if (curveEntries.size() > 256) throw std::runtime_error("curve query requires bounded samples");
            std::vector<MotionFeatureCurveSample> curves;
            for (std::size_t i = 0; i < curveEntries.size(); ++i) {
                auto item = curveEntries.get<ssq::Array>(i);
                if (item.size() != 3) throw std::runtime_error("curve sample requires name,time,value");
                curves.push_back({item.get<std::string>(0), item.get<float>(1), item.get<float>(2)});
            }
            return script::projectResult(bind.vm(), self->setFeatureQuery(*current, *previous, dt, samples, curves));
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    mm.addFunc("setLocomotionQuery", [bind](MotionMatcher* self, AnimPose* current, AnimPose* previous, float dt,
                                            ssq::Array entries) {
        try {
            if (!current || !previous || entries.size() != 5)
                throw std::runtime_error("locomotion query requires two poses and five trajectory samples");
            std::array<MotionLocomotionSample, 5> samples;
            for (std::size_t i = 0; i < 5; ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 7) throw std::runtime_error("locomotion sample requires x,y,z,vx,vy,vz,yaw");
                samples[i] = {item.get<float>(0), item.get<float>(1), item.get<float>(2), item.get<float>(3),
                              item.get<float>(4), item.get<float>(5), item.get<float>(6)};
            }
            return script::projectResult(bind.vm(), self->setLocomotionQuery(*current, *previous, dt, samples));
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    mm.addFunc("setCandidateRanges", [bind](MotionMatcher* self, ssq::Array entries) {
        try {
            if (entries.size() > 100000) throw std::runtime_error("too many candidate ranges");
            std::vector<MotionSearchRange> ranges;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 9)
                    throw std::runtime_error(
                        "candidate range requires "
                        "clip,start,end,bias,disableReselection,transitionBlocks,continuingBias,costOverrides,"
                        "continuingCostOverrides");
                MotionSearchRange range{item.get<int>(0), item.get<float>(1), item.get<float>(2), item.get<float>(3),
                                        item.get<bool>(4)};
                auto              blocks = item.get<ssq::Array>(5);
                if (blocks.size() > 100000) throw std::runtime_error("too many transition blocks");
                for (std::size_t j = 0; j < blocks.size(); ++j) {
                    auto block = blocks.get<ssq::Array>(j);
                    if (block.size() != 2) throw std::runtime_error("transition block requires start,end");
                    range.transitionBlocks.push_back({block.get<float>(0), block.get<float>(1)});
                }
                range.continuingCostBias = item.get<float>(6);
                for (std::size_t field : {std::size_t{7}, std::size_t{8}}) {
                    auto values = item.get<ssq::Array>(field);
                    if (values.size() > 100000) throw std::runtime_error("too many cost override intervals");
                    auto& destination = field == 7 ? range.costOverrides : range.continuingCostOverrides;
                    for (std::size_t j = 0; j < values.size(); ++j) {
                        auto value = values.get<ssq::Array>(j);
                        if (value.size() != 3) throw std::runtime_error("cost override requires start,end,bias");
                        destination.push_back({value.get<float>(0), value.get<float>(1), value.get<float>(2)});
                    }
                }
                ranges.push_back(std::move(range));
            }
            return script::projectResult(bind.vm(), self->setCandidateRanges(ranges),
                                         [](int count) { return Value(count); });
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    mm.addFunc("setQueryPose", [](MotionMatcher* self, AnimPose* pose) {
        if (!pose) throw std::runtime_error("query pose is required");
        auto configured = self->setQueryPose(*pose);
        if (!configured) throw std::runtime_error(configured.status().describe());
    });
    mm.addFunc("setTrajectory", [bind](MotionMatcher* self, ssq::Array entries) {
        try {
            if (entries.size() != 3) throw std::runtime_error("trajectory requires three samples");
            std::array<MotionTrajectorySample, 3> samples;
            for (std::size_t i = 0; i < 3; ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 3) throw std::runtime_error("trajectory sample requires x,z,yaw");
                samples[i] = {item.get<float>(0), item.get<float>(1), item.get<float>(2)};
            }
            return script::projectResult(bind.vm(), self->setTrajectory(samples));
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
}
}  // namespace eve::animation
