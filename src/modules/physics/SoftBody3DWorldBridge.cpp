#include "physics/softbody/SoftBody3D.h"

#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <memory>

namespace eve::physics {

namespace {

class World3DSoftBodyAdapter final : public softbody::ISoftBodyCollisionWorld {
public:
    explicit World3DSoftBodyAdapter(World3D* world) : world_(world) {}

    [[nodiscard]] bool softBodyCollisionAvailable() const noexcept override {
        return world_ && world_->isValid();
    }

    [[nodiscard]] softbody::SoftBodyContactState probeSoftBodyParticle(
        float x, float y, float z, float radius, softbody::SoftBodyContact& contact) const override {
        contact   = {};
        lastBody_ = nullptr;
        if (!world_) return softbody::SoftBodyContactState::None;

        ClothContact3D rigidContact;
        if (!world_->pointProbe(x, y, z, radius, &rigidContact) || !rigidContact.hit ||
            !rigidContact.body || !rigidContact.body->isValid())
            return softbody::SoftBodyContactState::None;

        lastBody_           = rigidContact.body;
        contact.hit         = true;
        contact.nx          = rigidContact.nx;
        contact.ny          = rigidContact.ny;
        contact.nz          = rigidContact.nz;
        contact.depth       = rigidContact.depth;
        contact.bodyId      = lastBody_->getId();
        contact.dynamicBody = lastBody_->getType() == "dynamic";
        return softbody::SoftBodyContactState::Hit;
    }

    void applySoftBodyImpulse(int bodyId, float x, float y, float z) override {
        if (!lastBody_ || !lastBody_->isValid() || lastBody_->getId() != bodyId ||
            lastBody_->getType() != "dynamic")
            return;
        lastBody_->applyLinearImpulse(x, y, z);
    }

private:
    World3D*        world_    = nullptr;
    mutable Body3D* lastBody_ = nullptr;
};

}  // namespace

void SoftBody3D::setCollideWorld(World3D* world) {
    world_ = world;
    ownedCollisionWorld_ = world ? std::make_unique<World3DSoftBodyAdapter>(world) : nullptr;
    collisionWorld_      = ownedCollisionWorld_.get();
}

World3D* SoftBody3D::getCollideWorld() const { return world_; }

}  // namespace eve::physics
