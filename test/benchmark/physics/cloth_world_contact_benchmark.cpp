// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"tests","default":"performance","module":"physics_cloth"}
#include <memory>
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;

TEST_CASE("softbody.cloth3d.collidesWithWorld3D") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                    *ground = world3->newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(8.f, 1.f, 8.f);

    std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(8, 6, 0.5f, 0.f, 4.f, 0.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    cloth->setGravity(0.f, -6.f, 0.f);
    cloth->setCollideWorld(world3.get());
    const float surfaceY = 0.f;
    for (int i = 0; i < 180; ++i) cloth->update(1.f / 60.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        REQUIRE_GE(cloth->getParticleY(i), surfaceY - cloth->getParticleSize() - 0.05f);
    // Sanity: the sheet fell from y = 4 toward the ground.
    REQUIRE_LT(cloth->getParticleY(5 * 8 + 3), 3.5f);
}

TEST_CASE("softbody.cloth3d.momentumExchangeScalesWithMass") {
    auto      *mod     = Physics::create();
    const auto runCase = [&](zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, float density) {
        std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, 0.f, 0.f, true));
        auto                    *box = world3->newBody("dynamic", 0.f, 1.2f, 0.f);
        REQUIRE(box != nullptr);
        box->newBoxShape(1.f, 0.8f, 1.f, density, 0.2f, 0.f);
        std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(6, 4, 0.4f, -0.6f, 3.f, -0.6f));
        for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
        cloth->setGravity(0.f, -5.f, 0.f);
        cloth->setCollideWorld(world3.get());
        for (int i = 0; i < 180; ++i) {
            world3->update(1.f / 60.f);
            cloth->update(1.f / 60.f);
        }
        return box->getLinearVelocityY();
    };

    const float vLight = runCase(_ZEROERR_TEST_CONTEXT, 0.05f);
    const float vHeavy = runCase(_ZEROERR_TEST_CONTEXT, 5.f);
    // +Y up: the light box is pushed downward noticeably, the heavy one barely.
    REQUIRE_LT(vLight, -0.2f);
    REQUIRE_LT(vLight, vHeavy - 0.1f);
}
