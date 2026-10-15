#include "graphics/Texture.h"
#include "particles/PcgMaterialSelector.h"
#include "particles/ParticleEmitter.h"
#include <zeroerr/unittest.h>
using namespace eve;
TEST_CASE("PcgMaterialSelector.skipsZeroAndIsDeterministic"){
    particles::PcgMaterialSelector selector; graphics::Texture reserved,a,b;
    REQUIRE(selector.add(&reserved).ok()); REQUIRE(selector.add(&a).ok()); REQUIRE(selector.add(&b).ok());
    auto* emitter=particles::ParticleEmitter::createEmitter(8);
    auto first=selector.selectAndApply(emitter,20260912u); REQUIRE(first.ok());
    CHECK(first.value()>=1); CHECK(first.value()<3);
    CHECK(emitter->getTexture()==(first.value()==1?&a:&b));
    auto replay=selector.selectAndApply(emitter,20260912u); REQUIRE(replay.ok()); CHECK_EQ(replay.value(),first.value());
    ecs::DestroyEntity(emitter);
}
TEST_CASE("PcgMaterialSelector.failureIsAtomic"){
    particles::PcgMaterialSelector selector; graphics::Texture old,only;
    auto* emitter=particles::ParticleEmitter::createEmitter(8); emitter->setTexture(&old);
    CHECK(!selector.add(nullptr).ok()); REQUIRE(selector.add(&only).ok());
    CHECK(!selector.selectAndApply(emitter,1u).ok()); CHECK(emitter->getTexture()==&old);
    CHECK(!selector.selectAndApply(nullptr,1u).ok()); ecs::DestroyEntity(emitter);
}
