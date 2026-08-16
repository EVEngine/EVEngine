#include "camera/CameraController.h"

#include "graphics/RenderSystem3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

namespace eve::camera {

Module_IMPL(Camera, new Camera());

CameraController::CameraController() = default;

void CameraController::setCamera(graphics::Camera3D *cam) { cam_ = cam; }

graphics::Camera3D *CameraController::getCamera() const { return cam_; }

void CameraController::setTarget(float x, float y, float z) {
    target_ = glm::vec3(x, y, z);
}

float CameraController::getTargetX() const { return target_.x; }
float CameraController::getTargetY() const { return target_.y; }
float CameraController::getTargetZ() const { return target_.z; }

void CameraController::setOffset(float x, float y, float z) {
    offset_ = glm::vec3(x, y, z);
}

void CameraController::setLookAhead(float x, float y, float z) {
    lookAhead_ = glm::vec3(x, y, z);
}

void CameraController::setMode(const std::string &mode) { mode_ = mode; }

std::string CameraController::getMode() const { return mode_; }

void CameraController::setRadius(float r) { radius_ = r; }
void CameraController::setAzimuth(float deg) { azimuthDeg_ = deg; }
void CameraController::setElevation(float deg) { elevationDeg_ = deg; }
void CameraController::setOrbitSpeed(float degPerSec) { orbitSpeedDeg_ = degPerSec; }

void CameraController::setYaw(float deg) { yawDeg_ = deg; }
void CameraController::setPitch(float deg) { pitchDeg_ = deg; }

void CameraController::setSmooth(float damping) { smooth_ = std::max(0.f, damping); }

void CameraController::setMaxSpeed(float unitsPerSec) { maxSpeed_ = std::max(0.f, unitsPerSec); }

void CameraController::addView(const std::string &name, float ex, float ey, float ez,
                               float tx, float ty, float tz) {
    View v;
    v.name = name;
    v.eye = glm::vec3(ex, ey, ez);
    v.target = glm::vec3(tx, ty, tz);
    views_.push_back(v);
}

bool CameraController::switchTo(const std::string &name, float blendTime) {
    int idx = -1;
    for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
        if (views_[i].name == name) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;

    if (viewIndex_ >= 0 && viewIndex_ < static_cast<int>(views_.size())) {
        blendFrom_ = views_[viewIndex_];
    } else {
        blendFrom_ = cur_;
    }
    blendTo_ = views_[idx];
    viewIndex_ = idx;

    if (blendTime > 0.f) {
        blending_ = true;
        blendDur_ = blendTime;
        blendT_ = 0.f;
    } else {
        blending_ = false;
        blendDur_ = 1.f;
        blendT_ = 0.f;
    }
    return true;
}

void CameraController::playSequence(float stepTime) {
    playing_ = true;
    stepTime_ = stepTime > 0.f ? stepTime : 1.f;
    timer_ = 0.f;
    if (!blending_ && views_.empty()) return;
    if (viewIndex_ < 0 || viewIndex_ >= static_cast<int>(views_.size())) {
        switchTo(views_.front().name, 0.f);
    }
}

void CameraController::stopSequence() { playing_ = false; }

bool CameraController::isPlaying() const { return playing_; }

CameraController::View CameraController::desired() const {
    View v;
    if (mode_ == "orbit") {
        const float az = glm::radians(azimuthDeg_);
        const float el = glm::radians(elevationDeg_);
        glm::vec3 dir(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
        v.eye = target_ + dir * radius_;
        v.target = target_;
    } else if (mode_ == "topdown") {
        v.eye = target_ + glm::vec3(0.f, radius_, 0.f);
        v.target = target_;
    } else if (mode_ == "firstperson") {
        v.eye = target_;
        const float yaw = glm::radians(yawDeg_);
        const float pitch = glm::radians(pitchDeg_);
        glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        v.target = v.eye + dir * 100.f;
    } else if (mode_ == "cinematic") {
        if (blending_ && viewIndex_ >= 0) {
            float t = blendDur_ > 0.f ? glm::clamp(blendT_ / blendDur_, 0.f, 1.f) : 1.f;
            t = t * t * (3.f - 2.f * t);  // smoothstep
            v.eye = glm::mix(blendFrom_.eye, blendTo_.eye, t);
            v.target = glm::mix(blendFrom_.target, blendTo_.target, t);
        } else if (viewIndex_ >= 0 && viewIndex_ < static_cast<int>(views_.size())) {
            v = views_[viewIndex_];
        } else {
            v.target = target_ + lookAhead_;
            v.eye = v.target + offset_;
        }
    } else {  // follow (默认)
        v.target = target_ + lookAhead_;
        v.eye = v.target + offset_;
    }
    return v;
}

void CameraController::applyView(const View &v) {
    if (!cam_) return;
    cam_->setEye(v.eye.x, v.eye.y, v.eye.z);
    cam_->setTarget(v.target.x, v.target.y, v.target.z);
}

void CameraController::easeToward(const View &v, float dt) {
    const float k = 1.f - std::exp(-smooth_ * dt);
    glm::vec3 deye = (v.eye - cur_.eye) * k;
    glm::vec3 dtgt = (v.target - cur_.target) * k;
    if (maxSpeed_ > 0.f && dt > 0.f) {
        const float maxStep = maxSpeed_ * dt;
        const float le = glm::length(deye);
        if (le > maxStep && le > 1e-6f) deye *= maxStep / le;
        const float lt = glm::length(dtgt);
        if (lt > maxStep && lt > 1e-6f) dtgt *= maxStep / lt;
    }
    cur_.eye += deye;
    cur_.target += dtgt;
}

void CameraController::snap() {
    cur_ = desired();
    initialized_ = true;
    applyView(cur_);
}

void CameraController::update(float dt) {
    if (!cam_) return;

    if (mode_ == "cinematic" && playing_ && !views_.empty()) {
        timer_ += dt;
        if (!blending_ && timer_ >= stepTime_) {
            const int next = (viewIndex_ + 1) % static_cast<int>(views_.size());
            switchTo(views_[next].name, blendDur_);
            timer_ = 0.f;
        }
    }

    if (blending_) {
        blendT_ += dt;
        if (blendT_ >= blendDur_) {
            blending_ = false;
            blendT_ = blendDur_;
        }
    }

    // orbit 自动盘旋：不断累积方位角
    if (mode_ == "orbit") {
        azimuthDeg_ += orbitSpeedDeg_ * dt;
    }

    const View d = desired();

    if (!initialized_) {
        cur_ = d;
        initialized_ = true;
    }
    easeToward(d, dt);
    applyView(cur_);
}

void Camera::expose(ssq::Class &cls) {
    // 模块级方法（如需要可在此添加）；当前只通过 eve.CameraController 暴露控制器。
}

void Camera::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Camera::create, false);
    expose(cls);

    auto cc = table.addClass<CameraController>(
        "CameraController",
        std::function<CameraController *()>([]() { return new CameraController(); }), true);

    cc.addFunc("setCamera", &CameraController::setCamera);
    cc.addFunc("getCamera", &CameraController::getCamera);

    cc.addFunc("setTarget", &CameraController::setTarget);
    cc.addFunc("getTargetX", &CameraController::getTargetX);
    cc.addFunc("getTargetY", &CameraController::getTargetY);
    cc.addFunc("getTargetZ", &CameraController::getTargetZ);
    cc.addFunc("setOffset", &CameraController::setOffset);
    cc.addFunc("setLookAhead", &CameraController::setLookAhead);

    cc.addFunc("setMode", &CameraController::setMode);
    cc.addFunc("getMode", &CameraController::getMode);

    cc.addFunc("setRadius", &CameraController::setRadius);
    cc.addFunc("setAzimuth", &CameraController::setAzimuth);
    cc.addFunc("setElevation", &CameraController::setElevation);
    cc.addFunc("setOrbitSpeed", &CameraController::setOrbitSpeed);

    cc.addFunc("setYaw", &CameraController::setYaw);
    cc.addFunc("setPitch", &CameraController::setPitch);

    cc.addFunc("setSmooth", &CameraController::setSmooth);
    cc.addFunc("setMaxSpeed", &CameraController::setMaxSpeed);
    cc.addFunc("snap", &CameraController::snap);

    cc.addFunc("addView", &CameraController::addView);
    cc.addFunc("switchTo", &CameraController::switchTo);
    cc.addFunc("playSequence", &CameraController::playSequence);
    cc.addFunc("stopSequence", &CameraController::stopSequence);
    cc.addFunc("isPlaying", &CameraController::isPlaying);

    cc.addFunc("update", &CameraController::update);
}

}  // namespace eve::camera
