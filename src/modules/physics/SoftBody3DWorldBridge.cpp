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

// SoftBody3D is exported from the backends group (its module is physics_softbody)
// while these two members are defined here, in the physics module's World-layer
// bridge, because they take a World3D. MSVC therefore reports C4273 "inconsistent
// dll linkage" for the definitions below: this translation unit sees the class as
// dllimport and then defines a member of it. The split is deliberate (see
// docs/dev/superpowers/specs/2026-08-18-test-suite-optimization.md section 7.16)
// and no other link unit imports these two symbols, so the warning is scoped to
// them rather than disabled for the build.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4273)
#endif

void SoftBody3D::setCollideWorld(World3D* world) {
    world_ = world;
    ownedCollisionWorld_ = world ? std::make_unique<World3DSoftBodyAdapter>(world) : nullptr;
    collisionWorld_      = ownedCollisionWorld_.get();
}

World3D* SoftBody3D::getCollideWorld() const { return world_; }

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace eve::physics
