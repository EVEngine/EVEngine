#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"

using namespace eve::rpg;

TEST_CASE("rpg.actorProtobuf.binaryRoundTripAndAtomicFailure") {
    RPGActor *source = RPGActor::createActor();
    REQUIRE(source != nullptr);
    source->setBaseAttribute("attack", 18.0);
    source->setBaseAttribute("hp", 100.0);
    source->setCurrent("hp", 47.0);
    REQUIRE(source->restoreProgression(4, 25.0, 120.0).ok());
    auto bytes = source->checkpointBinary();
    REQUIRE(bytes.ok());

    RPGActor *destination = RPGActor::createActor();
    REQUIRE(destination != nullptr);
    destination->setBaseAttribute("attack", 1.0);
    destination->setBaseAttribute("hp", 100.0);
    REQUIRE(destination->restoreCheckpointBinary(bytes.value()).ok());
    CHECK_EQ(destination->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(destination->getCurrent("hp"), 47.0);
    CHECK_EQ(destination->getLevel(), 4);

    CHECK(!destination->restoreCheckpointBinary("malformed protobuf").ok());
    CHECK(!destination->restoreCheckpointJson(
        R"({"schema":"eve.rpg.actor-checkpoint","version":2})").ok());
    CHECK_EQ(destination->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(destination->getCurrent("hp"), 47.0);
    CHECK_EQ(destination->getLevel(), 4);

    source->release();
    destination->release();
}
