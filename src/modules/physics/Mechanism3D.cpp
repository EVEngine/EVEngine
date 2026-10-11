#include "physics/Mechanism3D.h"

#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "common/Exception.h"

#include <cmath>

namespace eve::physics {

Mechanism3D::Mechanism3D(World3D *world, Kind kind, int id, Joint3D *drive, Joint3D *crankPin,
                         Joint3D *sliderPin, Joint3D *slider, float axisX, float axisY, float axisZ,
                         int ratchetDirection, float engagementTorque)
    : world_(world),
      kind_(kind),
      id_(id),
      drive_(drive),
      crankPin_(crankPin),
      sliderPin_(sliderPin),
      slider_(slider),
      axisX_(axisX),
      axisY_(axisY),
      axisZ_(axisZ),
      ratchetDirection_(ratchetDirection),
      engagementTorque_(engagementTorque) {}

Mechanism3D::~Mechanism3D() { destroy(); }

std::string Mechanism3D::getKind() const {
    switch (kind_) {
        case Kind::CrankSlider: return "crankSlider";
        case Kind::Shaft: return "shaft";
        case Kind::Ratchet: return "ratchet";
    }
    return "shaft";
}

bool Mechanism3D::isValid() const {
    if (!world_ || !drive_ || !drive_->isValid()) return false;
    if (kind_ == Kind::CrankSlider) {
        return crankPin_ && crankPin_->isValid() && sliderPin_ && sliderPin_->isValid() && slider_ &&
               slider_->isValid();
    }
    return true;
}

void Mechanism3D::requireKind(Kind expected, const char *operation) const {
    if (!isValid()) throw eve::Exception("%s: mechanism destroyed", operation);
    if (kind_ != expected) throw eve::Exception("%s: incompatible mechanism kind", operation);
}

float Mechanism3D::getDriveAngle() const {
    if (!isValid()) throw eve::Exception("Mechanism3D.getDriveAngle: mechanism destroyed");
    return drive_->getRevoluteAngle();
}

float Mechanism3D::getSliderTranslation() const {
    requireKind(Kind::CrankSlider, "Mechanism3D.getSliderTranslation");
    return slider_->getPrismaticTranslation();
}

float Mechanism3D::axisSpinSpeed() const {
    if (!isValid() || !drive_) return 0.f;
    Body3D *bodyA = world_ ? world_->findBodyById(drive_->getBodyAId()) : nullptr;
    Body3D *bodyB = world_ ? world_->findBodyById(drive_->getBodyBId()) : nullptr;
    if (!bodyA || !bodyB || !bodyA->isValid() || !bodyB->isValid()) return 0.f;
    const float wx = bodyB->getAngularVelocityX() - bodyA->getAngularVelocityX();
    const float wy = bodyB->getAngularVelocityY() - bodyA->getAngularVelocityY();
    const float wz = bodyB->getAngularVelocityZ() - bodyA->getAngularVelocityZ();
    return wx * axisX_ + wy * axisY_ + wz * axisZ_;
}

float Mechanism3D::getSpinSpeed() const {
    if (!isValid()) throw eve::Exception("Mechanism3D.getSpinSpeed: mechanism destroyed");
    return axisSpinSpeed();
}

void Mechanism3D::setDrive(float speed, float maxTorque) {
    if (!isValid()) throw eve::Exception("Mechanism3D.setDrive: mechanism destroyed");
    if (!std::isfinite(speed)) throw eve::Exception("Mechanism3D.setDrive: speed must be finite");
    if (!std::isfinite(maxTorque) || maxTorque < 0.f)
        throw eve::Exception("Mechanism3D.setDrive: maxTorque must be finite and >= 0");
    driveEnabled_ = true;
    driveSpeed_ = speed;
    driveMaxTorque_ = maxTorque;
    if (kind_ != Kind::Ratchet) {
        drive_->setRevoluteMotor(true, speed, maxTorque);
    }
}

void Mechanism3D::clearDrive() {
    if (!isValid()) throw eve::Exception("Mechanism3D.clearDrive: mechanism destroyed");
    driveEnabled_ = false;
    driveSpeed_ = 0.f;
    driveMaxTorque_ = 0.f;
    if (kind_ != Kind::Ratchet) {
        drive_->setRevoluteMotor(false, 0.f, 0.f);
    }
}

void Mechanism3D::setRatchetDirection(int direction) {
    requireKind(Kind::Ratchet, "Mechanism3D.setRatchetDirection");
    if (direction != 1 && direction != -1)
        throw eve::Exception("Mechanism3D.setRatchetDirection: direction must be +1 or -1");
    ratchetDirection_ = direction;
}

void Mechanism3D::setRatchetEngagementTorque(float torque) {
    requireKind(Kind::Ratchet, "Mechanism3D.setRatchetEngagementTorque");
    if (!std::isfinite(torque) || torque < 0.f)
        throw eve::Exception(
            "Mechanism3D.setRatchetEngagementTorque: torque must be finite and >= 0");
    engagementTorque_ = torque;
}

void Mechanism3D::syncBeforeStep() {
    if (kind_ != Kind::Ratchet || !isValid()) return;
    const float signedSpeed = axisSpinSpeed() * static_cast<float>(ratchetDirection_);
    if (signedSpeed < -1e-3f) {
        drive_->setRevoluteMotor(true, 0.f, engagementTorque_);
        return;
    }
    if (driveEnabled_) {
        const float driveSpeed = std::fabs(driveSpeed_) * static_cast<float>(ratchetDirection_);
        drive_->setRevoluteMotor(true, driveSpeed, driveMaxTorque_);
    } else {
        drive_->setRevoluteMotor(false, 0.f, 0.f);
    }
}

void Mechanism3D::invalidate() {
    if (world_) world_->forgetMechanism(this);
    world_ = nullptr;
    drive_ = nullptr;
    crankPin_ = nullptr;
    sliderPin_ = nullptr;
    slider_ = nullptr;
}

void Mechanism3D::destroy() {
    if (drive_ && drive_->isValid()) drive_->destroy();
    if (crankPin_ && crankPin_->isValid()) crankPin_->destroy();
    if (sliderPin_ && sliderPin_->isValid()) sliderPin_->destroy();
    if (slider_ && slider_->isValid()) slider_->destroy();
    if (world_) world_->forgetMechanism(this);
    invalidate();
}

}  // namespace eve::physics
