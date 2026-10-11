#include "camera/PcgCarCameraSetup.h"

#include "camera/CameraController.h"
#include "common/SquirrelBinding.h"
#include "graphics/RenderSystem3D.h"
#include "scene/SceneNodeRef.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

namespace eve::camera {
namespace {
Result<void> invalid(const char* message, const char* path) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, path));
}
bool finite(float value) { return std::isfinite(value); }
float shortestAngle(float from, float to) {
    float delta = std::fmod(to - from + 180.f, 360.f);
    if (delta < 0.f) delta += 360.f;
    return delta - 180.f;
}
}

Result<void> PcgCarCameraSetup::configure(const PcgCarCameraProfile& p) {
    if (!finite(p.targetHeight) || !finite(p.distance) || !finite(p.offsetFromWall) ||
        !finite(p.maximumDistance) || !finite(p.minimumDistance) || !finite(p.horizontalSpeed) ||
        !finite(p.verticalSpeed) || !finite(p.minimumPitch) || !finite(p.maximumPitch) ||
        !finite(p.zoomRate) || !finite(p.rotationDamping) || !finite(p.zoomDamping) ||
        p.targetHeight < 0.f || p.minimumDistance <= 0.f || p.maximumDistance < p.minimumDistance ||
        p.distance < p.minimumDistance || p.distance > p.maximumDistance || p.offsetFromWall < 0.f ||
        p.horizontalSpeed < 0.f || p.verticalSpeed < 0.f || p.maximumPitch < p.minimumPitch ||
        p.minimumPitch < -89.f || p.maximumPitch > 89.f || p.zoomRate < 0.f ||
        p.rotationDamping < 0.f || p.zoomDamping < 0.f)
        return invalid("invalid Pcg vehicle camera profile", "camera.pcgCar.configure");
    profile_ = p;
    configured_ = true;
    return Result<void>::success();
}

Result<void> PcgCarCameraSetup::apply(CameraController* c, graphics::Camera3D* camera,
                                       scene::SceneNodeRef* focus, float initialYaw, float initialPitch) {
    if (!c || !camera || !focus) return invalid("controller, camera and focus are required", "camera.pcgCar.apply");
    if (!finite(initialYaw) || !finite(initialPitch)) return invalid("initial angles must be finite", "camera.pcgCar.apply");
    if (!configured_) {
        auto result = configure(profile_);
        if (!result) return result;
    }
    auto limits = c->setRadiusLimits(profile_.minimumDistance, profile_.maximumDistance);
    if (!limits) return limits;
    yaw_ = initialYaw;
    pitch_ = std::clamp(initialPitch, profile_.minimumPitch, profile_.maximumPitch);
    c->setCamera(camera);
    c->setTargetNode(focus);
    c->setMode("orbit");
    c->setLookAhead(0.f, profile_.targetHeight, 0.f);
    c->setRadius(profile_.distance);
    c->setAzimuth(yaw_);
    c->setElevation(pitch_);
    c->setTargetSmooth(profile_.rotationDamping);
    c->setPositionSmooth(profile_.zoomDamping);
    c->setCollisionEnabled(true);
    c->setCollisionRadius(profile_.offsetFromWall);
    c->setCollisionRecovery(profile_.zoomDamping);
    c->setCollisionMask(profile_.collisionLayers);
    return Result<void>::success();
}

Result<void> PcgCarCameraSetup::update(CameraController* c, float mouseX, float mouseY,
                                        float scroll, float dt, bool targetIsMoving, float targetYaw) {
    if (!c) return invalid("controller is required", "camera.pcgCar.update");
    if (!finite(mouseX) || !finite(mouseY) || !finite(scroll) || !finite(dt) || !finite(targetYaw) || dt < 0.f)
        return invalid("camera input and dt must be finite", "camera.pcgCar.update");
    if (!configured_) return invalid("camera setup has not been configured", "camera.pcgCar.update");
    if (profile_.allowMouseHorizontal) yaw_ += mouseX * profile_.horizontalSpeed * 0.02f;
    if (profile_.allowMouseVertical)
        pitch_ = std::clamp(pitch_ - mouseY * profile_.verticalSpeed * 0.02f,
                            profile_.minimumPitch, profile_.maximumPitch);
    if ((profile_.lockToRearOfTarget || targetIsMoving) && std::abs(mouseX) <= 1e-6f) {
        const float blend = 1.f - std::exp(-profile_.rotationDamping * dt);
        yaw_ += shortestAngle(yaw_, targetYaw) * blend;
    }
    c->setAzimuth(yaw_);
    c->setElevation(pitch_);
    const float zoomDelta = -scroll * dt * profile_.zoomRate * std::abs(c->getRadius());
    c->addInput(0.f, 0.f, zoomDelta);
    return Result<void>::success();
}

void exposePcgCarCameraSetupBindings(ssq::Table& t) {
    auto p = t.addClass("PcgCarCameraProfile", ssq::Class::Ctor<PcgCarCameraProfile()>());
    p.addVar("targetHeight", &PcgCarCameraProfile::targetHeight); p.addVar("distance", &PcgCarCameraProfile::distance);
    p.addVar("offsetFromWall", &PcgCarCameraProfile::offsetFromWall); p.addVar("maximumDistance", &PcgCarCameraProfile::maximumDistance);
    p.addVar("minimumDistance", &PcgCarCameraProfile::minimumDistance); p.addVar("horizontalSpeed", &PcgCarCameraProfile::horizontalSpeed);
    p.addVar("verticalSpeed", &PcgCarCameraProfile::verticalSpeed); p.addVar("minimumPitch", &PcgCarCameraProfile::minimumPitch);
    p.addVar("maximumPitch", &PcgCarCameraProfile::maximumPitch); p.addVar("zoomRate", &PcgCarCameraProfile::zoomRate);
    p.addVar("rotationDamping", &PcgCarCameraProfile::rotationDamping); p.addVar("zoomDamping", &PcgCarCameraProfile::zoomDamping);
    p.addVar("collisionLayers", &PcgCarCameraProfile::collisionLayers); p.addVar("lockToRearOfTarget", &PcgCarCameraProfile::lockToRearOfTarget);
    p.addVar("allowMouseHorizontal", &PcgCarCameraProfile::allowMouseHorizontal); p.addVar("allowMouseVertical", &PcgCarCameraProfile::allowMouseVertical);
    auto s = t.addClass("PcgCarCameraSetup", ssq::Class::Ctor<PcgCarCameraSetup()>()); auto vm = t.getHandle();
    s.addFunc("configure", [vm](PcgCarCameraSetup* self, const PcgCarCameraProfile* profile) { return eve::script::projectResult(vm, profile ? self->configure(*profile) : invalid("profile is required", "camera.pcgCar.configure")); });
    s.addFunc("apply", [vm](PcgCarCameraSetup* self, CameraController* c, graphics::Camera3D* camera, scene::SceneNodeRef* focus, float yaw, float pitch) { return eve::script::projectResult(vm, self->apply(c, camera, focus, yaw, pitch)); });
    s.addFunc("update", [vm](PcgCarCameraSetup* self, CameraController* c, float mx, float my, float scroll, float dt, bool moving, float targetYaw) { return eve::script::projectResult(vm, self->update(c, mx, my, scroll, dt, moving, targetYaw)); });
    s.addFunc("getYaw", [](const PcgCarCameraSetup* self) { return self->getYaw(); });
    s.addFunc("getPitch", [](const PcgCarCameraSetup* self) { return self->getPitch(); });
}
}
