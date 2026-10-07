#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimationBindings.h"
#include "animation/OrientationWarping.h"
#include "common/SquirrelBinding.h"

namespace eve::animation {
namespace {

constexpr const char* kSource = "animation.bindings";

ssq::Table bindingFailure(HSQUIRRELVM vm, std::string message) {
    return script::projectResult(vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                             std::move(message), {}, {}, kSource)));
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
        if (!skeleton) return bindingFailure(vm, "configure requires a skeleton");
        try {
            auto spineBones = copyBoneArray(spine, "spine bones", 32);
            auto ikBones    = copyBoneArray(ik, "ik bones", 16);
            return script::projectResult(vm, self->configure(*skeleton, root, spineBones, ikBones));
        } catch (const std::exception& error) {
            return bindingFailure(vm, error.what());
        }
    });
    cls.addFunc("setDistributedAlpha", [vm = table.getHandle()](OrientationWarping* self, float alpha) {
        return script::projectResult(vm, self->setDistributedAlpha(alpha));
    });
    cls.addFunc("getDistributedAlpha", &OrientationWarping::getDistributedAlpha);
    cls.addFunc("setAngleThreshold", [vm = table.getHandle()](OrientationWarping* self, float radians) {
        return script::projectResult(vm, self->setAngleThreshold(radians));
    });
    cls.addFunc("getAngleThreshold", &OrientationWarping::getAngleThreshold);
    cls.addFunc("setRotationInterpSpeed", [vm = table.getHandle()](OrientationWarping* self, float speed) {
        return script::projectResult(vm, self->setRotationInterpSpeed(speed));
    });
    cls.addFunc("getRotationInterpSpeed", &OrientationWarping::getRotationInterpSpeed);
    cls.addFunc("setMinRootMotionSpeed", [vm = table.getHandle()](OrientationWarping* self, float speed) {
        return script::projectResult(vm, self->setMinRootMotionSpeed(speed));
    });
    cls.addFunc("getMinRootMotionSpeed", &OrientationWarping::getMinRootMotionSpeed);
    cls.addFunc("setEnabled", &OrientationWarping::setEnabled);
    cls.addFunc("isEnabled", &OrientationWarping::isEnabled);
    cls.addFunc("reset", &OrientationWarping::reset);
    cls.addFunc("apply", [vm = table.getHandle()](OrientationWarping* self, AnimPose* pose, float locomotionX,
                                                  float locomotionZ, float animatedX, float animatedZ, float dt) {
        if (!pose) return bindingFailure(vm, "apply requires a pose");
        return script::projectResult(vm, self->apply(*pose, locomotionX, locomotionZ, animatedX, animatedZ, dt));
    });
    cls.addFunc("getAppliedAngle", &OrientationWarping::getAppliedAngle);
    cls.addFunc("getTargetAngle", &OrientationWarping::getTargetAngle);
    cls.addFunc("getSkeleton", &OrientationWarping::getSkeleton);
    cls.addFunc("getRootBone", &OrientationWarping::getRootBone);
    cls.addFunc("getSpineBoneCount", &OrientationWarping::getSpineBoneCount);
    cls.addFunc("getIkBoneCount", &OrientationWarping::getIkBoneCount);
}
}  // namespace eve::animation
