#include "camera/CinemaDirector.h"

#include "common/SquirrelBinding.h"
#include "graphics/RenderSystem3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

namespace eve::camera {
namespace {

Result<void> fail(const char* message, const char* where) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, where));
}

bool finite3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float smoothstep01(float t) {
    t = glm::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

Result<void> CinemaDirector::bind(CameraController* controller) {
    if (!controller) return fail("controller is required", "camera.cinema.bind");
    if (isActive()) return fail("unbind is required before rebinding an active director", "camera.cinema.bind");
    controller_ = controller;
    cancelMove();
    return Result<void>::success();
}

Result<void> CinemaDirector::defineOverview(const std::string& name, float eyeX, float eyeY, float eyeZ,
                                            float targetX, float targetY, float targetZ, float fov) {
    if (name.empty()) return fail("overview name is required", "camera.cinema.defineOverview");
    const glm::vec3 eye(eyeX, eyeY, eyeZ);
    const glm::vec3 target(targetX, targetY, targetZ);
    if (!finite3(eye) || !finite3(target) || !std::isfinite(fov))
        return fail("overview pose must be finite", "camera.cinema.defineOverview");
    if (fov <= 0.f || fov >= 180.f) return fail("overview fov must be in (0,180)", "camera.cinema.defineOverview");
    if (glm::length(target - eye) < 1e-5f)
        return fail("overview eye and target must differ", "camera.cinema.defineOverview");
    for (auto& shot : overviews_) {
        if (shot.name == name) {
            shot.eye    = eye;
            shot.target = target;
            shot.fov    = fov;
            return Result<void>::success();
        }
    }
    overviews_.push_back(OverviewShot{name, eye, target, fov});
    return Result<void>::success();
}

Result<void> CinemaDirector::requireBound() const {
    if (!controller_) return fail("CinemaDirector is not bound", "camera.cinema");
    return Result<void>::success();
}

const CinemaDirector::OverviewShot* CinemaDirector::findOverview(const std::string& name) const {
    for (const auto& shot : overviews_)
        if (shot.name == name) return &shot;
    return nullptr;
}

Result<void> CinemaDirector::sampleLivePose(glm::vec3& eye, glm::vec3& target, float& fov) const {
    auto bound = requireBound();
    if (!bound) return bound;
    graphics::Camera3D* cam = controller_->getCamera();
    if (!cam) return fail("bound controller has no Camera3D", "camera.cinema.pose");
    eye    = glm::vec3(cam->getEyeX(), cam->getEyeY(), cam->getEyeZ());
    target = glm::vec3(cam->getTargetX(), cam->getTargetY(), cam->getTargetZ());
    fov    = cam->getFov();
    if (!finite3(eye) || !finite3(target) || !std::isfinite(fov))
        return fail("live camera pose is not finite", "camera.cinema.pose");
    return Result<void>::success();
}

Result<void> CinemaDirector::writeActivePose(const glm::vec3& eye, const glm::vec3& target, float fov) {
    if (stack_.empty()) return fail("no active overview to write", "camera.cinema.writePose");
    const std::string& name = stack_.back().overview;
    auto written = controller_->setView(name, eye.x, eye.y, eye.z, target.x, target.y, target.z, fov);
    if (!written) return written;
    // Keep the active cinematic index on this view without restarting a blend.
    if (!controller_->switchTo(name, 0.f))
        return fail("active overview view is missing", "camera.cinema.writePose");
    // Film moves author the pose each frame; snap so damping does not lag the track.
    controller_->snap();
    return Result<void>::success();
}

Result<void> CinemaDirector::applyOverviewPose(const OverviewShot& shot, float blendTime) {
    auto written =
        controller_->setView(shot.name, shot.eye.x, shot.eye.y, shot.eye.z, shot.target.x, shot.target.y,
                             shot.target.z, shot.fov);
    if (!written) return written;
    controller_->setMode("cinematic");
    controller_->stopSequence();
    controller_->setFov(shot.fov);
    if (!controller_->switchTo(shot.name, blendTime))
        return fail("failed to switch to overview view", "camera.cinema.pushOverview");
    if (blendTime <= 0.f) controller_->snap();
    return Result<void>::success();
}

Result<void> CinemaDirector::pushOverview(const std::string& name, float blendTime) {
    auto bound = requireBound();
    if (!bound) return bound;
    if (!std::isfinite(blendTime) || blendTime < 0.f)
        return fail("blendTime must be finite and >= 0", "camera.cinema.pushOverview");
    const OverviewShot* shot = findOverview(name);
    if (!shot) return fail("overview is not defined", "camera.cinema.pushOverview");

    StackEntry entry;
    entry.overview = name;
    if (stack_.empty()) {
        entry.gameplay       = controller_->captureFraming();
        entry.ownsGameplay   = true;
        if (controller_->isTimelinePlaying()) controller_->pauseTimeline();
        controller_->setDirectorEnabled(false);
        controller_->stopSequence();
    }
    cancelMove();
    stack_.push_back(entry);
    return applyOverviewPose(*shot, blendTime);
}

void CinemaDirector::cancelMove() { move_ = MoveState{}; }

Result<void> CinemaDirector::beginMove(MoveKind kind, float duration, float param) {
    auto bound = requireBound();
    if (!bound) return bound;
    if (!isActive()) return fail("push an overview before playing a film move", "camera.cinema.move");
    if (!std::isfinite(duration) || duration <= 0.f)
        return fail("move duration must be finite and > 0", "camera.cinema.move");
    if (!std::isfinite(param)) return fail("move parameter must be finite", "camera.cinema.move");

    glm::vec3 eye, target;
    float     fov = 60.f;
    auto sampled = sampleLivePose(eye, target, fov);
    if (!sampled) return sampled;

    MoveState next;
    next.active     = true;
    next.kind       = kind;
    next.duration   = duration;
    next.param      = param;
    next.fromEye    = eye;
    next.fromTarget = target;
    next.fromFov    = fov;
    next.toTarget   = target;
    next.toFov      = fov;

    const glm::vec3 forward = target - eye;
    const float     length  = glm::length(forward);
    if (length < 1e-5f) return fail("camera look direction is degenerate", "camera.cinema.move");
    const glm::vec3 dir = forward / length;

    if (kind == MoveKind::Dolly) {
        next.toEye = eye + dir * param;
        if (glm::length(target - next.toEye) < 1e-4f)
            return fail("dolly would collapse eye onto target", "camera.cinema.dolly");
    } else if (kind == MoveKind::Crane) {
        next.toEye = eye + glm::vec3(0.f, param, 0.f);
    } else if (kind == MoveKind::OrbitReveal) {
        next.orbitCenter    = target;
        const glm::vec3 rel = eye - target;
        next.orbitRadius    = glm::length(glm::vec2(rel.x, rel.z));
        if (next.orbitRadius < 1e-4f) return fail("orbit reveal needs horizontal radius", "camera.cinema.orbitReveal");
        next.orbitElevation = rel.y;
        next.orbitAzimuth0  = std::atan2(rel.x, rel.z);
        const float endAz   = next.orbitAzimuth0 + glm::radians(param);
        next.toEye =
            target + glm::vec3(std::sin(endAz) * next.orbitRadius, next.orbitElevation, std::cos(endAz) * next.orbitRadius);
    } else {
        return fail("unsupported move kind", "camera.cinema.move");
    }

    move_ = next;
    return Result<void>::success();
}

Result<void> CinemaDirector::dolly(float distance, float duration) {
    return beginMove(MoveKind::Dolly, duration, distance);
}

Result<void> CinemaDirector::crane(float deltaY, float duration) {
    return beginMove(MoveKind::Crane, duration, deltaY);
}

Result<void> CinemaDirector::orbitReveal(float degrees, float duration) {
    return beginMove(MoveKind::OrbitReveal, duration, degrees);
}

Result<void> CinemaDirector::pop(float blendTime) {
    auto bound = requireBound();
    if (!bound) return bound;
    if (stack_.empty()) return fail("story camera stack is empty", "camera.cinema.pop");
    if (!std::isfinite(blendTime) || blendTime < 0.f)
        return fail("blendTime must be finite and >= 0", "camera.cinema.pop");

    cancelMove();
    const StackEntry top = stack_.back();
    stack_.pop_back();

    if (stack_.empty()) {
        auto restored = controller_->restoreFraming(top.gameplay, blendTime);
        if (!restored) {
            stack_.push_back(top);
            return restored;
        }
        return Result<void>::success();
    }

    const OverviewShot* previous = findOverview(stack_.back().overview);
    if (!previous) {
        stack_.push_back(top);
        return fail("previous overview is missing", "camera.cinema.pop");
    }
    return applyOverviewPose(*previous, blendTime);
}

Result<void> CinemaDirector::update(float dt) {
    auto bound = requireBound();
    if (!bound) return bound;
    if (!std::isfinite(dt) || dt < 0.f) return fail("dt must be finite and >= 0", "camera.cinema.update");
    if (!move_.active) return Result<void>::success();

    move_.age += dt;
    const float t = smoothstep01(move_.duration > 0.f ? move_.age / move_.duration : 1.f);
    glm::vec3   eye    = glm::mix(move_.fromEye, move_.toEye, t);
    glm::vec3   target = glm::mix(move_.fromTarget, move_.toTarget, t);
    float       fov    = glm::mix(move_.fromFov, move_.toFov, t);

    if (move_.kind == MoveKind::OrbitReveal) {
        const float az = move_.orbitAzimuth0 + glm::radians(move_.param) * t;
        eye = move_.orbitCenter +
              glm::vec3(std::sin(az) * move_.orbitRadius, move_.orbitElevation, std::cos(az) * move_.orbitRadius);
        target = move_.orbitCenter;
        fov    = move_.fromFov;
    }

    auto written = writeActivePose(eye, target, fov);
    if (!written) return written;

    if (move_.age >= move_.duration) {
        auto finalWrite = writeActivePose(move_.kind == MoveKind::OrbitReveal ? eye : move_.toEye,
                                          move_.kind == MoveKind::OrbitReveal ? target : move_.toTarget,
                                          move_.kind == MoveKind::OrbitReveal ? fov : move_.toFov);
        cancelMove();
        return finalWrite;
    }
    return Result<void>::success();
}

std::string CinemaDirector::getActiveOverview() const {
    return stack_.empty() ? std::string() : stack_.back().overview;
}

void exposeCinemaDirectorBindings(ssq::Table& table) {
    auto cls = table.addClass<CinemaDirector>(
        "CinemaDirector", std::function<CinemaDirector*()>([]() { return new CinemaDirector(); }), true);
    auto* vm = table.getHandle();
    cls.addFunc("bind", [vm](CinemaDirector* self, CameraController* controller) {
        return eve::script::projectResult(vm, self->bind(controller));
    });
    cls.addFunc("defineOverview",
                [vm](CinemaDirector* self, const std::string& name, float ex, float ey, float ez, float tx, float ty,
                     float tz, float fov) {
                    return eve::script::projectResult(vm, self->defineOverview(name, ex, ey, ez, tx, ty, tz, fov));
                });
    cls.addFunc("pushOverview", [vm](CinemaDirector* self, const std::string& name, float blend) {
        return eve::script::projectResult(vm, self->pushOverview(name, blend));
    });
    cls.addFunc("dolly", [vm](CinemaDirector* self, float distance, float duration) {
        return eve::script::projectResult(vm, self->dolly(distance, duration));
    });
    cls.addFunc("crane", [vm](CinemaDirector* self, float deltaY, float duration) {
        return eve::script::projectResult(vm, self->crane(deltaY, duration));
    });
    cls.addFunc("orbitReveal", [vm](CinemaDirector* self, float degrees, float duration) {
        return eve::script::projectResult(vm, self->orbitReveal(degrees, duration));
    });
    cls.addFunc("pop", [vm](CinemaDirector* self, float blend) {
        return eve::script::projectResult(vm, self->pop(blend));
    });
    cls.addFunc("update", [vm](CinemaDirector* self, float dt) {
        return eve::script::projectResult(vm, self->update(dt));
    });
    cls.addFunc("isActive", [](const CinemaDirector* self) { return self->isActive(); });
    cls.addFunc("depth", [](const CinemaDirector* self) { return self->depth(); });
    cls.addFunc("isMovePlaying", [](const CinemaDirector* self) { return self->isMovePlaying(); });
    cls.addFunc("getActiveOverview", [](const CinemaDirector* self) { return self->getActiveOverview(); });
}

}  // namespace eve::camera
