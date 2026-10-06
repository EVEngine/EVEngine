#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimationBindings.h"
#include "animation/OrientationWarping.h"

namespace eve::animation {
namespace {
ssq::Table resultTable(HSQUIRRELVM vm, const eve::Result<void>& applied) {
    ssq::Table result(vm);
    result.set("ok", applied.ok());
    result.set("message", applied.status().describe());
    return result;
}

std::vector<int> copyBoneArray(ssq::Array values, const char* label, int maximum) {
    if (values.size() > static_cast<std::size_t>(maximum))
        throw std::runtime_error(std::string(label) + " exceeds the supported bone count");
    std::vector<int> bones;
    bones.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) bones.push_back(values.get<int>(i));
    return bones;
}
}  // namespace

void exposeOrientationWarpingBindings(ssq::Table& table) {
    auto cls = table.addClass<OrientationWarping>(
        "OrientationWarping", std::function<OrientationWarping*()>([] { return new OrientationWarping(); }), true);
    cls.addFunc("configure", [vm = table.getHandle()](OrientationWarping* self, AnimSkeleton* skeleton, int root,
                                                      ssq::Array spine, ssq::Array ik) {
        if (!skeleton) {
            ssq::Table result(vm);
            result.set("ok", false);
            result.set("message", std::string("configure requires a skeleton"));
            return result;
        }
        try {
            auto spineBones = copyBoneArray(spine, "spine bones", 32);
            auto ikBones    = copyBoneArray(ik, "ik bones", 16);
            return resultTable(vm, self->configure(*skeleton, root, spineBones, ikBones));
        } catch (const std::exception& error) {
            ssq::Table result(vm);
            result.set("ok", false);
            result.set("message", std::string(error.what()));
            return result;
        }
    });
    cls.addFunc("setDistributedAlpha", [vm = table.getHandle()](OrientationWarping* self, float alpha) {
        return resultTable(vm, self->setDistributedAlpha(alpha));
    });
    cls.addFunc("getDistributedAlpha", &OrientationWarping::getDistributedAlpha);
    cls.addFunc("setAngleThreshold", [vm = table.getHandle()](OrientationWarping* self, float radians) {
        return resultTable(vm, self->setAngleThreshold(radians));
    });
    cls.addFunc("getAngleThreshold", &OrientationWarping::getAngleThreshold);
    cls.addFunc("setRotationInterpSpeed", [vm = table.getHandle()](OrientationWarping* self, float speed) {
        return resultTable(vm, self->setRotationInterpSpeed(speed));
    });
    cls.addFunc("getRotationInterpSpeed", &OrientationWarping::getRotationInterpSpeed);
    cls.addFunc("setMinRootMotionSpeed", [vm = table.getHandle()](OrientationWarping* self, float speed) {
        return resultTable(vm, self->setMinRootMotionSpeed(speed));
    });
    cls.addFunc("getMinRootMotionSpeed", &OrientationWarping::getMinRootMotionSpeed);
    cls.addFunc("setEnabled", &OrientationWarping::setEnabled);
    cls.addFunc("isEnabled", &OrientationWarping::isEnabled);
    cls.addFunc("reset", &OrientationWarping::reset);
    cls.addFunc("apply", [vm = table.getHandle()](OrientationWarping* self, AnimPose* pose, float locomotionX,
                                                  float locomotionZ, float animatedX, float animatedZ, float dt) {
        if (!pose) {
            ssq::Table result(vm);
            result.set("ok", false);
            result.set("message", std::string("apply requires a pose"));
            return result;
        }
        return resultTable(vm, self->apply(*pose, locomotionX, locomotionZ, animatedX, animatedZ, dt));
    });
    cls.addFunc("getAppliedAngle", &OrientationWarping::getAppliedAngle);
    cls.addFunc("getTargetAngle", &OrientationWarping::getTargetAngle);
    cls.addFunc("getSkeleton", &OrientationWarping::getSkeleton);
    cls.addFunc("getRootBone", &OrientationWarping::getRootBone);
    cls.addFunc("getSpineBoneCount", &OrientationWarping::getSpineBoneCount);
    cls.addFunc("getIkBoneCount", &OrientationWarping::getIkBoneCount);
}
}  // namespace eve::animation
