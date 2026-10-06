#include <functional>
#include <string>
#include <simplesquirrel/simplesquirrel.hpp>
#include "animation/AnimationBindings.h"
#include "animation/AnimPose.h"
#include "animation/PhysicalBalancePose.h"

namespace eve::animation {
namespace {

ssq::Table resultTable(HSQUIRRELVM vm, const eve::Result<void>& result) {
    ssq::Table table(vm);
    table.set("ok", result.ok());
    table.set("message", result.status().describe());
    return table;
}

}  // namespace

void exposePhysicalBalancePoseBindings(ssq::Table& table) {
    auto pose = table.addClass<PhysicalBalancePose>(
        "PhysicalBalancePose", std::function<PhysicalBalancePose*()>([]() -> PhysicalBalancePose* { return nullptr; }),
        true);
    pose.addFunc("getSkeleton", &PhysicalBalancePose::getSkeleton);
    pose.addFunc("setSupportBone", [vm = table.getHandle()](PhysicalBalancePose* self, int bone) {
        return resultTable(vm, self->setSupportBone(bone));
    });
    pose.addFunc("getSupportBone", &PhysicalBalancePose::getSupportBone);
    pose.addFunc("setBalanceBone", [vm = table.getHandle()](PhysicalBalancePose* self, int bone) {
        return resultTable(vm, self->setBalanceBone(bone));
    });
    pose.addFunc("getBalanceBone", &PhysicalBalancePose::getBalanceBone);
    pose.addFunc("setBoneMass", [vm = table.getHandle()](PhysicalBalancePose* self, int bone, float mass) {
        return resultTable(vm, self->setBoneMass(bone, mass));
    });
    pose.addFunc("getBoneMass", &PhysicalBalancePose::getBoneMass);
    pose.addFunc("setRecovery",
                 [vm = table.getHandle()](PhysicalBalancePose* self, float frequencyHz, float dampingZeta) {
                     return resultTable(vm, self->setRecovery(frequencyHz, dampingZeta));
                 });
    pose.addFunc("getRecoveryFrequency", &PhysicalBalancePose::getRecoveryFrequency);
    pose.addFunc("getRecoveryDamping", &PhysicalBalancePose::getRecoveryDamping);
    pose.addFunc("setRecoil",
                 [vm = table.getHandle()](PhysicalBalancePose* self, float frequencyHz, float dampingZeta) {
                     return resultTable(vm, self->setRecoil(frequencyHz, dampingZeta));
                 });
    pose.addFunc("getRecoilFrequency", &PhysicalBalancePose::getRecoilFrequency);
    pose.addFunc("getRecoilDamping", &PhysicalBalancePose::getRecoilDamping);
    pose.addFunc("setGravity", [vm = table.getHandle()](PhysicalBalancePose* self, float gravity) {
        return resultTable(vm, self->setGravity(gravity));
    });
    pose.addFunc("getGravity", &PhysicalBalancePose::getGravity);
    pose.addFunc("setPendulumHeight", [vm = table.getHandle()](PhysicalBalancePose* self, float height) {
        return resultTable(vm, self->setPendulumHeight(height));
    });
    pose.addFunc("getPendulumHeight", &PhysicalBalancePose::getPendulumHeight);
    pose.addFunc("setInertia", [vm = table.getHandle()](PhysicalBalancePose* self, float inertia) {
        return resultTable(vm, self->setInertia(inertia));
    });
    pose.addFunc("getInertia", &PhysicalBalancePose::getInertia);
    pose.addFunc("setRecoilInertia", [vm = table.getHandle()](PhysicalBalancePose* self, float inertia) {
        return resultTable(vm, self->setRecoilInertia(inertia));
    });
    pose.addFunc("getRecoilInertia", &PhysicalBalancePose::getRecoilInertia);
    pose.addFunc("setMaxLean", [vm = table.getHandle()](PhysicalBalancePose* self, float radians) {
        return resultTable(vm, self->setMaxLean(radians));
    });
    pose.addFunc("getMaxLean", &PhysicalBalancePose::getMaxLean);
    pose.addFunc("setTargetPose", [vm = table.getHandle()](PhysicalBalancePose* self, AnimPose* target) {
        return resultTable(vm, self->setTargetPose(target));
    });
    pose.addFunc("snapToTarget", [vm = table.getHandle()](PhysicalBalancePose* self) {
        return resultTable(vm, self->snapToTarget());
    });
    pose.addFunc("applyImpulse", [vm = table.getHandle()](PhysicalBalancePose* self, int bone, float ix, float iy,
                                                          float iz, float px, float py, float pz) {
        return resultTable(vm, self->applyImpulse(bone, ix, iy, iz, px, py, pz));
    });
    pose.addFunc("update", &PhysicalBalancePose::update);
    pose.addFunc("getPose", &PhysicalBalancePose::getPose);
    pose.addFunc("getTargetPose", &PhysicalBalancePose::getTargetPose);
    pose.addFunc("getLeanX", &PhysicalBalancePose::getLeanX);
    pose.addFunc("getLeanZ", &PhysicalBalancePose::getLeanZ);
    pose.addFunc("getLeanVelocityX", &PhysicalBalancePose::getLeanVelocityX);
    pose.addFunc("getLeanVelocityZ", &PhysicalBalancePose::getLeanVelocityZ);
    pose.addFunc("getCenterOfMassX", &PhysicalBalancePose::getCenterOfMassX);
    pose.addFunc("getCenterOfMassY", &PhysicalBalancePose::getCenterOfMassY);
    pose.addFunc("getCenterOfMassZ", &PhysicalBalancePose::getCenterOfMassZ);
    pose.addFunc("getSupportX", &PhysicalBalancePose::getSupportX);
    pose.addFunc("getSupportY", &PhysicalBalancePose::getSupportY);
    pose.addFunc("getSupportZ", &PhysicalBalancePose::getSupportZ);
}

}  // namespace eve::animation
