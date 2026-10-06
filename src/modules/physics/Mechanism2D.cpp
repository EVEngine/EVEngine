#include "physics/Mechanism2D.h"

#include "physics/Body.h"
#include "physics/Joint2D.h"
#include "physics/World.h"
#include "common/Exception.h"

#include <cmath>

namespace eve::physics {

Mechanism2D::Mechanism2D(World *world, Kind kind, int id, Joint2D *drive, Joint2D *crankPin,
                         Joint2D *sliderPin, Joint2D *slider, int ratchetDirection,
                         float engagementTorque)
    : world_(world),
      kind_(kind),
      id_(id),
      drive_(drive),
      crankPin_(crankPin),
      sliderPin_(sliderPin),
      slider_(slider),
      ratchetDirection_(ratchetDirection),
      engagementTorque_(engagementTorque) {}

Mechanism2D::~Mechanism2D() { destroy(); }

std::string Mechanism2D::getKind() const {
    switch (kind_) {
        case Kind::CrankSlider: return "crankSlider";
        case Kind::Shaft: return "shaft";
        case Kind::Ratchet: return "ratchet";
    }
    return "shaft";
}

bool Mechanism2D::isValid() const {
    if (!world_ || !drive_ || !drive_->isValid()) return false;
    if (kind_ == Kind::CrankSlider) {
        return crankPin_ && crankPin_->isValid() && sliderPin_ && sliderPin_->isValid() && slider_ &&
               slider_->isValid();
    }
    return true;
}

void Mechanism2D::requireKind(Kind expected, const char *operation) const {
    if (!isValid()) throw eve::Exception("%s: mechanism destroyed", operation);
    if (kind_ != expected) throw eve::Exception("%s: incompatible mechanism kind", operation);
}

float Mechanism2D::getDriveAngle() const {
    if (!isValid()) throw eve::Exception("Mechanism2D.getDriveAngle: mechanism destroyed");
    return drive_->getRevoluteAngle();
}

float Mechanism2D::getSliderTranslation() const {
    requireKind(Kind::CrankSlider, "Mechanism2D.getSliderTranslation");
    return slider_->getPrismaticTranslation();
}

float Mechanism2D::axisSpinSpeed() const {
    if (!isValid() || !drive_) return 0.f;
    return drive_->getRevoluteSpeed();
}

float Mechanism2D::getSpinSpeed() const {
    if (!isValid()) throw eve::Exception("Mechanism2D.getSpinSpeed: mechanism destroyed");
    return axisSpinSpeed();
}

void Mechanism2D::setDrive(float speed, float maxTorque) {
    if (!isValid()) throw eve::Exception("Mechanism2D.setDrive: mechanism destroyed");
    if (!std::isfinite(speed)) throw eve::Exception("Mechanism2D.setDrive: speed must be finite");
    if (!std::isfinite(maxTorque) || maxTorque < 0.f)
        throw eve::Exception("Mechanism2D.setDrive: maxTorque must be finite and >= 0");
    driveEnabled_ = true;
    driveSpeed_ = speed;
    driveMaxTorque_ = maxTorque;
    if (kind_ != Kind::Ratchet) drive_->setRevoluteMotor(true, speed, maxTorque);
}

void Mechanism2D::clearDrive() {
    if (!isValid()) throw eve::Exception("Mechanism2D.clearDrive: mechanism destroyed");
    driveEnabled_ = false;
    driveSpeed_ = 0.f;
    driveMaxTorque_ = 0.f;
    if (kind_ != Kind::Ratchet) drive_->setRevoluteMotor(false, 0.f, 0.f);
}

void Mechanism2D::setRatchetDirection(int direction) {
    requireKind(Kind::Ratchet, "Mechanism2D.setRatchetDirection");
    if (direction != 1 && direction != -1)
        throw eve::Exception("Mechanism2D.setRatchetDirection: direction must be +1 or -1");
    ratchetDirection_ = direction;
}

void Mechanism2D::setRatchetEngagementTorque(float torque) {
    requireKind(Kind::Ratchet, "Mechanism2D.setRatchetEngagementTorque");
    if (!std::isfinite(torque) || torque < 0.f)
        throw eve::Exception(
            "Mechanism2D.setRatchetEngagementTorque: torque must be finite and >= 0");
    engagementTorque_ = torque;
}

void Mechanism2D::syncBeforeStep() {
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

void Mechanism2D::invalidate() {
    if (world_) world_->forgetMechanism(this);
    world_ = nullptr;
    drive_ = nullptr;
    crankPin_ = nullptr;
    sliderPin_ = nullptr;
    slider_ = nullptr;
}

void Mechanism2D::destroy() {
    if (drive_ && drive_->isValid()) drive_->destroy();
    if (crankPin_ && crankPin_->isValid()) crankPin_->destroy();
    if (sliderPin_ && sliderPin_->isValid()) sliderPin_->destroy();
    if (slider_ && slider_->isValid()) slider_->destroy();
    if (world_) world_->forgetMechanism(this);
    invalidate();
}

}  // namespace eve::physics
