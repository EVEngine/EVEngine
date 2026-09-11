#include "physics/cloth/ClothModule.h"

#include "gpgpu/Gpgpu.h"
#include "physics/cloth/Cloth.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothGPU.h"
#include "physics/cloth/ClothModel.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::cloth {

Module_IMPL(Cloth, new Cloth());

eve::physics::Cloth* Cloth::newCloth(int cols, int rows, float spacing, float originX, float originY) {
    return new eve::physics::Cloth(cols, rows, spacing, originX, originY);
}

eve::physics::Cloth3D* Cloth::newCloth3D(int cols, int rows, float spacing, float originX, float originY,
                                         float originZ) {
    return new eve::physics::Cloth3D(cols, rows, spacing, originX, originY, originZ);
}

std::unique_ptr<eve::physics::Cloth3D> Cloth::createCloth(const eve::physics::ClothModel& model) {
    return std::make_unique<eve::physics::Cloth3D>(model);
}

eve::physics::ClothGPU* Cloth::newClothGPU(int cols, int rows, float spacing, float originX, float originY) {
    auto* gpgpu = eve::ModuleManager::getInstance<eve::gpgpu::Gpgpu>("Gpgpu");
    if (!gpgpu) gpgpu = eve::gpgpu::Gpgpu::create();
    return new eve::physics::ClothGPU(gpgpu, cols, rows, spacing, originX, originY);
}

void Cloth::expose(ssq::Table& table) {
    auto module = table.addClass(name, Cloth::create, false);
    expose(module);

    using Cloth    = eve::physics::Cloth;
    using Cloth3D  = eve::physics::Cloth3D;
    using ClothGPU = eve::physics::ClothGPU;

    auto cloth = table.addClass<Cloth>("Cloth", std::function<Cloth*()>([]() -> Cloth* { return nullptr; }), true);
    cloth.addFunc("update", &Cloth::update);
    cloth.addFunc("setGravity", &Cloth::setGravity);
    cloth.addFunc("getGravityX", &Cloth::getGravityX);
    cloth.addFunc("getGravityY", &Cloth::getGravityY);
    cloth.addFunc("setStiffness", &Cloth::setStiffness);
    cloth.addFunc("getStiffness", &Cloth::getStiffness);
    cloth.addFunc("setIterations", &Cloth::setIterations);
    cloth.addFunc("getIterations", &Cloth::getIterations);
    cloth.addFunc("setDamping", &Cloth::setDamping);
    cloth.addFunc("getDamping", &Cloth::getDamping);
    cloth.addFunc("setParticleSize", &Cloth::setParticleSize);
    cloth.addFunc("getParticleSize", &Cloth::getParticleSize);
    cloth.addFunc("setParticleMass", &Cloth::setParticleMass);
    cloth.addFunc("getParticleMass", &Cloth::getParticleMass);
    cloth.addFunc("setSelfCollision", &Cloth::setSelfCollision);
    cloth.addFunc("getSelfCollision", &Cloth::getSelfCollision);
    cloth.addFunc("setFoldStiffness", &Cloth::setFoldStiffness);
    cloth.addFunc("getFoldStiffness", &Cloth::getFoldStiffness);
    cloth.addFunc("setMaxFoldAngle", &Cloth::setMaxFoldAngle);
    cloth.addFunc("getMaxFoldAngle", &Cloth::getMaxFoldAngle);
    cloth.addFunc("setBounds", &Cloth::setBounds);
    cloth.addFunc("clearBounds", &Cloth::clearBounds);
    cloth.addFunc("pin", &Cloth::pin);
    cloth.addFunc("unpin", &Cloth::unpin);
    cloth.addFunc("pinTopRow", &Cloth::pinTopRow);
    cloth.addFunc("isPinned", &Cloth::isPinned);
    cloth.addFunc("grabAt", &Cloth::grabAt);
    cloth.addFunc("moveGrab", &Cloth::moveGrab);
    cloth.addFunc("releaseGrab", &Cloth::releaseGrab);
    cloth.addFunc("isGrabbing", &Cloth::isGrabbing);
    cloth.addFunc("getGrabIndex", &Cloth::getGrabIndex);
    cloth.addFunc("applyForce", &Cloth::applyForce);
    cloth.addFunc("interactAt", &Cloth::interactAt);
    cloth.addFunc("setCollideWorld", &Cloth::setCollideWorld);
    cloth.addFunc("getCollideWorld", &Cloth::getCollideWorld);
    cloth.addFunc("reset", &Cloth::reset);
    cloth.addFunc("setColor", &Cloth::setColor);
    cloth.addFunc("draw", &Cloth::draw);
    cloth.addFunc("getCols", &Cloth::getCols);
    cloth.addFunc("getRows", &Cloth::getRows);
    cloth.addFunc("getParticleCount", &Cloth::getParticleCount);
    cloth.addFunc("getParticleX", &Cloth::getParticleX);
    cloth.addFunc("getParticleY", &Cloth::getParticleY);
    cloth.addFunc("setParticlePosition", &Cloth::setParticlePosition);
    cloth.addFunc("getSpacing", &Cloth::getSpacing);
    cloth.addFunc("destroy", &Cloth::destroy);

    auto cloth3 =
        table.addClass<Cloth3D>("Cloth3D", std::function<Cloth3D*()>([]() -> Cloth3D* { return nullptr; }), true);
    cloth3.addFunc("update", &Cloth3D::update);
    cloth3.addFunc("setGravity", &Cloth3D::setGravity);
    cloth3.addFunc("getGravityX", &Cloth3D::getGravityX);
    cloth3.addFunc("getGravityY", &Cloth3D::getGravityY);
    cloth3.addFunc("getGravityZ", &Cloth3D::getGravityZ);
    cloth3.addFunc("setStiffness", &Cloth3D::setStiffness);
    cloth3.addFunc("getStiffness", &Cloth3D::getStiffness);
    cloth3.addFunc("setStretchCompliance", &Cloth3D::setStretchCompliance);
    cloth3.addFunc("getStretchCompliance", &Cloth3D::getStretchCompliance);
    cloth3.addFunc("setShearCompliance", &Cloth3D::setShearCompliance);
    cloth3.addFunc("getShearCompliance", &Cloth3D::getShearCompliance);
    cloth3.addFunc("setBendCompliance", &Cloth3D::setBendCompliance);
    cloth3.addFunc("getBendCompliance", &Cloth3D::getBendCompliance);
    cloth3.addFunc("setTetherScale", &Cloth3D::setTetherScale);
    cloth3.addFunc("getTetherScale", &Cloth3D::getTetherScale);
    cloth3.addFunc("setTetherCompliance", &Cloth3D::setTetherCompliance);
    cloth3.addFunc("getTetherCompliance", &Cloth3D::getTetherCompliance);
    cloth3.addFunc("setPressure", &Cloth3D::setPressure);
    cloth3.addFunc("getPressure", &Cloth3D::getPressure);
    cloth3.addFunc("getBackendName", &Cloth3D::getBackendName);
    cloth3.addFunc("supportsFeature", &Cloth3D::supportsFeature);
    cloth3.addFunc("setVolumeCompliance", &Cloth3D::setVolumeCompliance);
    cloth3.addFunc("getVolumeCompliance", &Cloth3D::getVolumeCompliance);
    cloth3.addFunc("getCurrentVolume", &Cloth3D::getCurrentVolume);
    cloth3.addFunc("setIterations", &Cloth3D::setIterations);
    cloth3.addFunc("getIterations", &Cloth3D::getIterations);
    cloth3.addFunc("setDamping", &Cloth3D::setDamping);
    cloth3.addFunc("getDamping", &Cloth3D::getDamping);
    cloth3.addFunc("setParticleSize", &Cloth3D::setParticleSize);
    cloth3.addFunc("getParticleSize", &Cloth3D::getParticleSize);
    cloth3.addFunc("setParticleMass", &Cloth3D::setParticleMass);
    cloth3.addFunc("getParticleMass", &Cloth3D::getParticleMass);
    cloth3.addFunc("setSelfCollision", &Cloth3D::setSelfCollision);
    cloth3.addFunc("getSelfCollision", &Cloth3D::getSelfCollision);
    cloth3.addFunc("setFoldStiffness", &Cloth3D::setFoldStiffness);
    cloth3.addFunc("getFoldStiffness", &Cloth3D::getFoldStiffness);
    cloth3.addFunc("setMaxFoldAngle", &Cloth3D::setMaxFoldAngle);
    cloth3.addFunc("getMaxFoldAngle", &Cloth3D::getMaxFoldAngle);
    cloth3.addFunc("setBounds", &Cloth3D::setBounds);
    cloth3.addFunc("clearBounds", &Cloth3D::clearBounds);
    cloth3.addFunc("setCollisionMaterial", &Cloth3D::setCollisionMaterial);
    cloth3.addFunc("getCollisionFriction", &Cloth3D::getCollisionFriction);
    cloth3.addFunc("getCollisionRestitution", &Cloth3D::getCollisionRestitution);
    cloth3.addFunc("setCollisionFilter", &Cloth3D::setCollisionFilter);
    cloth3.addFunc("getCollisionCategoryBits", &Cloth3D::getCollisionCategoryBits);
    cloth3.addFunc("getCollisionMaskBits", &Cloth3D::getCollisionMaskBits);
    cloth3.addFunc("setTearThreshold", &Cloth3D::setTearThreshold);
    cloth3.addFunc("getTearThreshold", &Cloth3D::getTearThreshold);
    cloth3.addFunc("setMaxTearsPerStep", &Cloth3D::setMaxTearsPerStep);
    cloth3.addFunc("getMaxTearsPerStep", &Cloth3D::getMaxTearsPerStep);
    cloth3.addFunc("tearConstraint", &Cloth3D::tearConstraint);
    cloth3.addFunc("getTornConstraintCount", &Cloth3D::getTornConstraintCount);
    cloth3.addFunc("pin", &Cloth3D::pin);
    cloth3.addFunc("unpin", &Cloth3D::unpin);
    cloth3.addFunc("pinTopRow", &Cloth3D::pinTopRow);
    cloth3.addFunc("isPinned", &Cloth3D::isPinned);
    cloth3.addFunc("setParticleInverseMass", &Cloth3D::setParticleInverseMass);
    cloth3.addFunc("getParticleInverseMass", &Cloth3D::getParticleInverseMass);
    cloth3.addFunc("setSkinConstraint", &Cloth3D::setSkinConstraint);
    cloth3.addFunc("updateSkinReference", &Cloth3D::updateSkinReference);
    cloth3.addFunc("clearSkinConstraint", &Cloth3D::clearSkinConstraint);
    cloth3.addFunc("hasSkinConstraint", &Cloth3D::hasSkinConstraint);
    cloth3.addFunc("attachParticle", &Cloth3D::attachParticle);
    cloth3.addFunc("updateAttachment", &Cloth3D::updateAttachment);
    cloth3.addFunc("detachParticle", &Cloth3D::detachParticle);
    cloth3.addFunc("isAttached", &Cloth3D::isAttached);
    cloth3.addFunc("grabAt", &Cloth3D::grabAt);
    cloth3.addFunc("moveGrab", &Cloth3D::moveGrab);
    cloth3.addFunc("releaseGrab", &Cloth3D::releaseGrab);
    cloth3.addFunc("isGrabbing", &Cloth3D::isGrabbing);
    cloth3.addFunc("getGrabIndex", &Cloth3D::getGrabIndex);
    cloth3.addFunc("applyForce", &Cloth3D::applyForce);
    cloth3.addFunc("setWindVelocity", &Cloth3D::setWindVelocity);
    cloth3.addFunc("getWindVelocityX", &Cloth3D::getWindVelocityX);
    cloth3.addFunc("getWindVelocityY", &Cloth3D::getWindVelocityY);
    cloth3.addFunc("getWindVelocityZ", &Cloth3D::getWindVelocityZ);
    cloth3.addFunc("setAerodynamics", &Cloth3D::setAerodynamics);
    cloth3.addFunc("getAirDensity", &Cloth3D::getAirDensity);
    cloth3.addFunc("getDragCoefficient", &Cloth3D::getDragCoefficient);
    cloth3.addFunc("getLiftCoefficient", &Cloth3D::getLiftCoefficient);
    cloth3.addFunc("interactAt", &Cloth3D::interactAt);
    cloth3.addFunc("setCollideWorld", &Cloth3D::setCollideWorld);
    cloth3.addFunc("getCollideWorld", &Cloth3D::getCollideWorld);
    cloth3.addFunc("reset", &Cloth3D::reset);
    cloth3.addFunc("setColor", &Cloth3D::setColor);
    cloth3.addFunc("draw", &Cloth3D::draw);
    cloth3.addFunc("getCols", &Cloth3D::getCols);
    cloth3.addFunc("getRows", &Cloth3D::getRows);
    cloth3.addFunc("getParticleCount", &Cloth3D::getParticleCount);
    cloth3.addFunc("getTriangleCount", &Cloth3D::getTriangleCount);
    cloth3.addFunc("getDistanceConstraintCount", &Cloth3D::getDistanceConstraintCount);
    cloth3.addFunc("getTetherConstraintCount", &Cloth3D::getTetherConstraintCount);
    cloth3.addFunc("getSkinConstraintCount", &Cloth3D::getSkinConstraintCount);
    cloth3.addFunc("getAttachmentCount", &Cloth3D::getAttachmentCount);
    cloth3.addFunc("getParticleX", &Cloth3D::getParticleX);
    cloth3.addFunc("getParticleY", &Cloth3D::getParticleY);
    cloth3.addFunc("getParticleZ", &Cloth3D::getParticleZ);
    cloth3.addFunc("setParticlePosition", &Cloth3D::setParticlePosition);
    cloth3.addFunc("getSpacing", &Cloth3D::getSpacing);
    cloth3.addFunc("getOriginX", &Cloth3D::getOriginX);
    cloth3.addFunc("getOriginY", &Cloth3D::getOriginY);
    cloth3.addFunc("getOriginZ", &Cloth3D::getOriginZ);
    cloth3.addFunc("destroy", &Cloth3D::destroy);

    auto clothGpu =
        table.addClass<ClothGPU>("ClothGPU", std::function<ClothGPU*()>([]() -> ClothGPU* { return nullptr; }), true);
    clothGpu.addFunc("update", &ClothGPU::update);
    clothGpu.addFunc("getBackendName", &ClothGPU::getBackendName);
    clothGpu.addFunc("supportsFeature", &ClothGPU::supportsFeature);
    clothGpu.addFunc("setGravity", &ClothGPU::setGravity);
    clothGpu.addFunc("getGravityX", &ClothGPU::getGravityX);
    clothGpu.addFunc("getGravityY", &ClothGPU::getGravityY);
    clothGpu.addFunc("setStiffness", &ClothGPU::setStiffness);
    clothGpu.addFunc("getStiffness", &ClothGPU::getStiffness);
    clothGpu.addFunc("setIterations", &ClothGPU::setIterations);
    clothGpu.addFunc("getIterations", &ClothGPU::getIterations);
    clothGpu.addFunc("setDamping", &ClothGPU::setDamping);
    clothGpu.addFunc("getDamping", &ClothGPU::getDamping);
    clothGpu.addFunc("setParticleSize", &ClothGPU::setParticleSize);
    clothGpu.addFunc("getParticleSize", &ClothGPU::getParticleSize);
    clothGpu.addFunc("setSelfCollision", &ClothGPU::setSelfCollision);
    clothGpu.addFunc("getSelfCollision", &ClothGPU::getSelfCollision);
    clothGpu.addFunc("setBounds", &ClothGPU::setBounds);
    clothGpu.addFunc("clearBounds", &ClothGPU::clearBounds);
    clothGpu.addFunc("pin", &ClothGPU::pin);
    clothGpu.addFunc("unpin", &ClothGPU::unpin);
    clothGpu.addFunc("pinTopRow", &ClothGPU::pinTopRow);
    clothGpu.addFunc("isPinned", &ClothGPU::isPinned);
    clothGpu.addFunc("applyForce", &ClothGPU::applyForce);
    clothGpu.addFunc("interactAt", &ClothGPU::interactAt);
    clothGpu.addFunc("setColor", &ClothGPU::setColor);
    clothGpu.addFunc("draw", &ClothGPU::draw);
    clothGpu.addFunc("getCols", &ClothGPU::getCols);
    clothGpu.addFunc("getRows", &ClothGPU::getRows);
    clothGpu.addFunc("getParticleCount", &ClothGPU::getParticleCount);
    clothGpu.addFunc("getParticleX", &ClothGPU::getParticleX);
    clothGpu.addFunc("getParticleY", &ClothGPU::getParticleY);
    clothGpu.addFunc("getSpacing", &ClothGPU::getSpacing);
    clothGpu.addFunc("getOriginX", &ClothGPU::getOriginX);
    clothGpu.addFunc("getOriginY", &ClothGPU::getOriginY);
    clothGpu.addFunc("reset", &ClothGPU::reset);
    clothGpu.addFunc("destroy", &ClothGPU::destroy);
}

void Cloth::expose(ssq::Class& cls) {
    cls.addFunc("newCloth", &eve::cloth::Cloth::newCloth);
    cls.addFunc("newCloth3D", &eve::cloth::Cloth::newCloth3D);
    cls.addFunc("newClothGPU", &eve::cloth::Cloth::newClothGPU);
}

}  // namespace eve::cloth
