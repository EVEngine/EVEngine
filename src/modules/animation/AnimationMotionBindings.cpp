#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include "animation/AnimationBindings.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "common/SquirrelOwnership.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve::animation {
// A provider read hands over a freshly allocated FileData that this call owns.
using eve::script::Owned;
void exposeMotionMatcherBindings(ssq::Table& table) {
    exposeAnimInertializerBindings(table);
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
    db.addFunc("setLocomotionFeatures",
               [vm = table.getHandle()](MotionDatabase* self, int left, int right, int pelvis) {
                   auto       configured = self->setLocomotionFeatures(left, right, pelvis);
                   ssq::Table result(vm);
                   result.set("ok", configured.ok());
                   result.set("message", configured.status().describe());
                   return result;
               });
    db.addFunc("hasLocomotionFeatures", &MotionDatabase::hasLocomotionFeatures);
    db.addFunc("hasFeatureLayout", &MotionDatabase::hasFeatureLayout);
    db.addFunc("loadFeatureCurves", [vm = table.getHandle()](MotionDatabase* self, std::string path, ssq::Array sources) {
        ssq::Table result(vm);
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
            auto loaded = self->setFeatureCurves({static_cast<const std::byte*>(data->getData()), data->getSize()}, names);
            result.set("ok", loaded.ok()); result.set("message", loaded.status().describe());
        } catch (const std::exception& e) { result.set("ok", false); result.set("message", std::string(e.what())); }
        return result;
    });
    db.addFunc("setFeatureLayout", [vm = table.getHandle()](MotionDatabase* self, int rate, ssq::Array entries, float lengthScale) {
        ssq::Table result(vm);
        try {
            if (entries.size() == 0 || entries.size() > 256) throw std::runtime_error("feature layout requires 1..256 channels");
            MotionFeatureLayout layout; layout.sampleRate = rate;layout.normalizationLengthScale = lengthScale;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 12 && item.size() != 13) throw std::runtime_error("feature channel requires twelve fields plus an optional curve name");
                MotionFeatureChannel c;
                const auto kind = item.get<std::string>(0), source = item.get<std::string>(1), query = item.get<std::string>(2);
                if (kind == "position") c.kind = MotionFeatureKind::Position;
                else if (kind == "velocity") c.kind = MotionFeatureKind::Velocity;
                else if (kind == "heading") c.kind = MotionFeatureKind::Heading;
                else if (kind == "curve") c.kind = MotionFeatureKind::Curve;
                else throw std::runtime_error("unsupported feature operation");
                if (source == "pose") c.source = MotionFeatureSource::Pose;
                else if (source == "trajectory") c.source = MotionFeatureSource::Trajectory;
                else throw std::runtime_error("unsupported feature source");
                if (query == "USE_CHARACTER_POSE") c.query = MotionFeatureQuery::Character;
                else if (query == "USE_CONTINUING_POSE") c.query = MotionFeatureQuery::Continuing;
                else throw std::runtime_error("unsupported feature query policy");
                c.bone = item.get<int>(3); c.origin = item.get<int>(4);
                const int axes = item.get<int>(5);
                if (axes < 1 || axes > 7) throw std::runtime_error("feature axes must be a nonempty XYZ mask");
                c.axes = static_cast<std::uint8_t>(axes); c.headingAxis = item.get<int>(6);
                c.sampleTime = item.get<float>(7); c.weight = item.get<float>(8);
                c.characterSpaceVelocity = item.get<bool>(9); c.normalizeVelocity = item.get<bool>(10);
                c.normalizationGroup = item.get<std::string>(11);
                if (item.size() == 13) c.curve = item.get<std::string>(12);
                layout.channels.push_back(std::move(c));
            }
            auto configured = self->setFeatureLayout(layout);
            result.set("ok", configured.ok()); result.set("message", configured.status().describe());
        } catch (const std::exception& e) { result.set("ok", false); result.set("message", std::string(e.what())); }
        return result;
    });
    db.addFunc("setFeatureNormalizationRanges", [vm = table.getHandle()](MotionDatabase* self, ssq::Array entries) {
        ssq::Table result(vm);
        try {
            if (entries.size() > 100000) throw std::runtime_error("too many normalization ranges");
            std::vector<MotionNormalizationRange> ranges;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 3) throw std::runtime_error("normalization range requires clip,start,end");
                ranges.push_back({item.get<int>(0), item.get<float>(1), item.get<float>(2)});
            }
            auto configured = self->setFeatureNormalizationRanges(ranges);
            result.set("ok", configured.ok());
            result.set("message", configured.status().describe());
            result.set("count", configured.ok() ? configured.value() : 0);
        } catch (const std::exception& e) {
            result.set("ok", false); result.set("message", std::string(e.what())); result.set("count", 0);
        }
        return result;
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
    mm.addFunc("setPlayRateRange", [vm = table.getHandle()](MotionMatcher* self, float minimum, float maximum) {
        auto configured = self->setPlayRateRange(minimum, maximum);
        ssq::Table result(vm); result.set("ok", configured.ok()); result.set("message", configured.status().describe()); return result;
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
    mm.addFunc("setPoseReselectHistory", [vm = table.getHandle()](MotionMatcher* self, float seconds) {
        auto configured = self->setPoseReselectHistory(seconds);
        ssq::Table result(vm); result.set("ok", configured.ok()); result.set("message", configured.status().describe()); return result;
    });
    mm.addFunc("getPoseReselectHistory", &MotionMatcher::getPoseReselectHistory);
    mm.addFunc("getMatchedFrame", &MotionMatcher::getMatchedFrame);
    mm.addFunc("getMatchedClipIndex", &MotionMatcher::getMatchedClipIndex);
    mm.addFunc("getMatchedTime", &MotionMatcher::getMatchedTime);
    mm.addFunc("getLastSearchCost", &MotionMatcher::getLastSearchCost);
    mm.addFunc("getPose", &MotionMatcher::getPose);
    mm.addFunc("search", &MotionMatcher::search);
    mm.addFunc("update", &MotionMatcher::update);
    mm.addFunc("setFeatureQuery", [vm = table.getHandle()](MotionMatcher* self, AnimPose* current,
                                                          AnimPose* previous, float dt, ssq::Array entries, ssq::Array curveEntries) {
        ssq::Table result(vm);
        try {
            if (!current || !previous || entries.size() > 256) throw std::runtime_error("feature query requires two poses and bounded trajectory samples");
            std::vector<MotionFeatureTrajectorySample> samples;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 8) throw std::runtime_error("feature sample requires time,x,y,z,vx,vy,vz,yaw");
                samples.push_back({item.get<float>(0),item.get<float>(1),item.get<float>(2),item.get<float>(3),
                    item.get<float>(4),item.get<float>(5),item.get<float>(6),item.get<float>(7)});
            }
            if (curveEntries.size() > 256) throw std::runtime_error("curve query requires bounded samples");
            std::vector<MotionFeatureCurveSample> curves;
            for (std::size_t i = 0; i < curveEntries.size(); ++i) {
                auto item = curveEntries.get<ssq::Array>(i);
                if (item.size() != 3) throw std::runtime_error("curve sample requires name,time,value");
                curves.push_back({item.get<std::string>(0), item.get<float>(1), item.get<float>(2)});
            }
            auto configured = self->setFeatureQuery(*current, *previous, dt, samples, curves);
            result.set("ok", configured.ok()); result.set("message", configured.status().describe());
        } catch (const std::exception& e) { result.set("ok", false); result.set("message", std::string(e.what())); }
        return result;
    });
    mm.addFunc("setLocomotionQuery", [vm = table.getHandle()](MotionMatcher* self, AnimPose* current,
                                                              AnimPose* previous, float dt, ssq::Array entries) {
        ssq::Table result(vm);
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
            auto configured = self->setLocomotionQuery(*current, *previous, dt, samples);
            result.set("ok", configured.ok());
            result.set("message", configured.status().describe());
        } catch (const std::exception& e) {
            result.set("ok", false);
            result.set("message", std::string(e.what()));
        }
        return result;
    });
    mm.addFunc("setCandidateRanges", [vm = table.getHandle()](MotionMatcher* self, ssq::Array entries) {
        ssq::Table result(vm);
        try {
            if (entries.size() > 100000) throw std::runtime_error("too many candidate ranges");
            std::vector<MotionSearchRange> ranges;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 9) throw std::runtime_error("candidate range requires clip,start,end,bias,disableReselection,transitionBlocks,continuingBias,costOverrides,continuingCostOverrides");
                MotionSearchRange range{item.get<int>(0), item.get<float>(1), item.get<float>(2),
                                        item.get<float>(3), item.get<bool>(4)};
                auto blocks = item.get<ssq::Array>(5);
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
            auto selected = self->setCandidateRanges(ranges);
            result.set("ok", selected.ok());
            result.set("message", selected.status().describe());
            result.set("count", selected.ok() ? selected.value() : 0);
        } catch (const std::exception& e) {
            result.set("ok", false);
            result.set("message", std::string(e.what()));
            result.set("count", 0);
        }
        return result;
    });
    mm.addFunc("setQueryPose", [](MotionMatcher* self, AnimPose* pose) {
        if (!pose) throw std::runtime_error("query pose is required");
        auto configured = self->setQueryPose(*pose);
        if (!configured) throw std::runtime_error(configured.status().describe());
    });
    mm.addFunc("setTrajectory", [vm = table.getHandle()](MotionMatcher* self, ssq::Array entries) {
        ssq::Table result(vm);
        try {
            if (entries.size() != 3) throw std::runtime_error("trajectory requires three samples");
            std::array<MotionTrajectorySample, 3> samples;
            for (std::size_t i = 0; i < 3; ++i) {
                auto item = entries.get<ssq::Array>(i);
                if (item.size() != 3) throw std::runtime_error("trajectory sample requires x,z,yaw");
                samples[i] = {item.get<float>(0), item.get<float>(1), item.get<float>(2)};
            }
            auto configured = self->setTrajectory(samples);
            result.set("ok", configured.ok());
            result.set("message", configured.status().describe());
        } catch (const std::exception& e) {
            result.set("ok", false);
            result.set("message", std::string(e.what()));
        }
        return result;
    });
}
}  // namespace eve::animation
