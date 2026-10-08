#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimationBindings.h"
#include "animation/OrientationWarping.h"
#include "common/SquirrelBindContext.h"
#include "common/SquirrelBinding.h"

namespace eve::animation {
namespace {

constexpr const char* kSource = "animation.bindings";

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
    const script::BindContext bind{table.getHandle(), kSource};
    auto                      cls = table.addClass<OrientationWarping>(
        "OrientationWarping", std::function<OrientationWarping*()>([] { return new OrientationWarping(); }), true);
    script::bindMethod(
        cls, "configure",
        [bind](OrientationWarping* self, AnimSkeleton* skeleton, int root, ssq::Array spine, ssq::Array ik) {
            if (!skeleton) return bind.failInvalid("configure requires a skeleton");
            try {
                auto spineBones = copyBoneArray(spine, "spine bones", 32);
                auto ikBones    = copyBoneArray(ik, "ik bones", 16);
                return script::projectResult(bind.vm(), self->configure(*skeleton, root, spineBones, ikBones));
            } catch (const std::exception& error) {
                return bind.failInvalid(error.what());
            }
        });
    script::bindMethod(cls, "setDistributedAlpha", [bind](OrientationWarping* self, float alpha) {
        return script::projectResult(bind.vm(), self->setDistributedAlpha(alpha));
    });
    script::bindMethod(cls, "getDistributedAlpha", &OrientationWarping::getDistributedAlpha);
    script::bindMethod(cls, "setAngleThreshold", [bind](OrientationWarping* self, float radians) {
        return script::projectResult(bind.vm(), self->setAngleThreshold(radians));
    });
    script::bindMethod(cls, "getAngleThreshold", &OrientationWarping::getAngleThreshold);
    script::bindMethod(cls, "setRotationInterpSpeed", [bind](OrientationWarping* self, float speed) {
        return script::projectResult(bind.vm(), self->setRotationInterpSpeed(speed));
    });
    script::bindMethod(cls, "getRotationInterpSpeed", &OrientationWarping::getRotationInterpSpeed);
    script::bindMethod(cls, "setMinRootMotionSpeed", [bind](OrientationWarping* self, float speed) {
        return script::projectResult(bind.vm(), self->setMinRootMotionSpeed(speed));
    });
    script::bindMethod(cls, "getMinRootMotionSpeed", &OrientationWarping::getMinRootMotionSpeed);
    script::bindMethod(cls, "setEnabled", &OrientationWarping::setEnabled);
    script::bindMethod(cls, "isEnabled", &OrientationWarping::isEnabled);
    script::bindMethod(cls, "reset", &OrientationWarping::reset);
    script::bindMethod(cls, "apply",
                       [bind](OrientationWarping* self, AnimPose* pose, float locomotionX, float locomotionZ,
                              float animatedX, float animatedZ, float dt) {
                           if (!pose) return bind.failInvalid("apply requires a pose");
                           return script::projectResult(
                               bind.vm(), self->apply(*pose, locomotionX, locomotionZ, animatedX, animatedZ, dt));
                       });
    script::bindMethod(cls, "getAppliedAngle", &OrientationWarping::getAppliedAngle);
    script::bindMethod(cls, "getTargetAngle", &OrientationWarping::getTargetAngle);
    script::bindMethod(cls, "getSkeleton", &OrientationWarping::getSkeleton);
    script::bindMethod(cls, "getRootBone", &OrientationWarping::getRootBone);
    script::bindMethod(cls, "getSpineBoneCount", &OrientationWarping::getSpineBoneCount);
    script::bindMethod(cls, "getIkBoneCount", &OrientationWarping::getIkBoneCount);
}
}  // namespace eve::animation
