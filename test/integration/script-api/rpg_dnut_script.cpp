#include "common/ECS.h"
#include "common/Module.h"
#include "rpg/RpgDialect.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

/**
 * @brief Drive a `.dnut` story from Squirrel with the script owning movement.
 *
 * The story declares *that* an actor moves; the script decides what moving
 * means for its own game. That split is the whole point of the dialect: one
 * authored story survives a tile RPG, a tactics board or a continuous world,
 * because the engine never interprets the coordinates and never grows a
 * movement model of its own.
 *
 * This is also the only end-to-end cover of the `eve.RPG()` story surface:
 * publish -> session -> step stream -> effect.
 */
TEST_CASE("rpg.dnut.scriptOwnsMovementAndVerifiesTheEffect") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        moveRequestSeen <- "";
        local rpg = eve.RPG();
        local source = "story demo.walk {\n"
                     + "    move actor=hero x=3 y=4 duration=0.5\n"
                     + "    variable name=step op=set value=1\n"
                     + "    message text=Arrived\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "demo.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local session = rpg.newStorySession();
            local began = session.begin("demo.walk", gs, null, null, null);
            if (began.ok && session.getStepKind() == "move" &&
                session.getStepString("actor") == "hero" &&
                session.getStepNumber("x") == 3.0 &&
                session.getStepNumber("y") == 4.0 &&
                session.getStepNumber("duration") == 0.5) {
                moveRequestSeen = session.getStepString("actor") + ":" +
                                  session.getStepNumber("x") + "," + session.getStepNumber("y");
                // The script's own movement model runs here. This one is
                // instantaneous; a real game would walk its actor and only
                // acknowledge the step once the actor had actually arrived.
                local advanced = session.advance();
                if (advanced.ok && session.getStepKind() == "message" &&
                    gs.getVariable("step") == 1.0 &&
                    session.getStepString("text") == "Arrived") {
                    local finished = session.advance();
                    if (finished.ok && !session.isActive()) result = "ok";
                }
            }
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
    // Effect verification: the script saw the authored move verbatim.
    CHECK_EQ(vm.find("moveRequestSeen").toString().find("hero:3"), std::size_t(0));

    eve::rpg::RpgStoryCatalogue::clear();
}

/** @brief A story a script never acknowledges stays suspended and keeps its cursor. */
TEST_CASE("rpg.dnut.scriptStaysOnTheMoveStepUntilItAcknowledges") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        local rpg = eve.RPG();
        local source = "story demo.hold {\n"
                     + "    move actor=hero x=1 y=2\n"
                     + "    message text=Arrived\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "hold.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local session = rpg.newStorySession();
            local began = session.begin("demo.hold", gs, null, null, null);
            if (began.ok && session.isActive() && session.isBlocked() &&
                session.getStepKind() == "move" && session.getStoryId() == "demo.hold") {
                // Frame 1 ends here: the host is still walking the actor.
                local stillMoving = session.getStepKind() == "move";
                // Frame 2: the host acknowledges the finished move.
                local advanced = session.advance();
                if (stillMoving && advanced.ok &&
                    session.getStepKind() == "message" &&
                    session.getStepString("text") == "Arrived") {
                    if (session.advance().ok && !session.isActive()) result = "ok";
                }
            }
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    eve::rpg::RpgStoryCatalogue::clear();
}

/** @brief A script sees the compile diagnostic of a broken story, and the old catalogue survives. */
TEST_CASE("rpg.dnut.scriptSeesStructuredFailureForBrokenContent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        local rpg = eve.RPG();
        local good = rpg.replaceStoriesFromDnut("story keep.me {\n    message text=hi\n}\n", "keep.dnut");
        if (good.ok) {
            local bad = rpg.replaceStoriesFromDnut("story broken.me {\n    message\n}\n", "broken.dnut");
            if (!bad.ok && rpg.hasStory("keep.me") && rpg.getStoryCount() == 1) result = "ok";
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    eve::rpg::RpgStoryCatalogue::clear();
}

/**
 * @brief A real party reaches the domain steps through the nullable script surface.
 *
 * `begin` accepts the optional collaborators as Squirrel objects so that `null`
 * is expressible. This case pins the other half of that contract: an actual
 * `RPGParty` still arrives as the C++ type the dialect resolves actors from.
 */
TEST_CASE("rpg.dnut.scriptResolvesActorsThroughARealParty") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        local rpg = eve.RPG();
        local source = "story demo.party {\n"
                     + "    skill actor=hero learn=fireball\n"
                     + "    variable name=gold op=set value=7\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "party.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local party = rpg.newParty();
            local hero = rpg.newActor();
            local added = party.addMember("hero", hero);
            local session = rpg.newStorySession();
            local began = session.begin("demo.party", gs, party, null, null);
            if (added.ok && began.ok && !session.isActive() &&
                hero.knowsSkill("fireball") && gs.getVariable("gold") == 7.0) {
                result = "ok";
            }
            if (hero != null) hero.release();
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    eve::rpg::RpgStoryCatalogue::clear();
}

/**
 * @brief Drive a `.dnut` story from Squirrel with the script owning playback.
 *
 * The sibling of `rpg.dnut.scriptOwnsMovementAndVerifiesTheEffect`: the story
 * declares *that* a clip plays and with which flags, while the script decides
 * what a clip means for its own animation stack. `clip` is never resolved by the
 * engine, so the same authored story survives a skeletal rig, a sprite sequence
 * or a Spine setup.
 *
 * The C++ playback port (`RpgStoryBinding::playAnimation`) is not exposed to
 * Squirrel, exactly like `moveActor`: a script drives playback through the
 * host-presented contract — read the payload, then acknowledge the step.
 */
TEST_CASE("rpg.dnut.scriptOwnsAnimationAndVerifiesTheEffect") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQL(
        result <- "fail";
        clipRequestSeen <- "";
        local rpg = eve.RPG();
        local source = "story demo.emote {\n"
                     + "    animation target=hero clip=emote.cheer loop=true hold=false\n"
                     + "    variable name=step op=set value=1\n"
                     + "    message text=Cheered\n"
                     + "}\n";
        local published = rpg.replaceStoriesFromDnut(source, "emote.dnut");
        if (published.ok) {
            local gs = rpg.newGameState();
            local session = rpg.newStorySession();
            local began = session.begin("demo.emote", gs, null, null, null);
            if (began.ok && session.getStepKind() == "animation" &&
                session.getStepString("target") == "hero" &&
                session.getStepString("clip") == "emote.cheer" &&
                session.getStepBool("loop") && !session.getStepBool("hold")) {
                clipRequestSeen = session.getStepString("target") + ":" + session.getStepString("clip");
                // The script's own playback model runs here. This one is
                // instantaneous; a real game would start the clip and only
                // acknowledge the step once it had actually finished.
                local advanced = session.advance();
                if (advanced.ok && session.getStepKind() == "message" &&
                    gs.getVariable("step") == 1.0 &&
                    session.getStepString("text") == "Cheered") {
                    local finished = session.advance();
                    if (finished.ok && !session.isActive()) result = "ok";
                }
            }
        }
    )SQL"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
    // Effect verification: the script saw the authored clip request verbatim.
    CHECK_EQ(vm.find("clipRequestSeen").toString(), std::string("hero:emote.cheer"));

    eve::rpg::RpgStoryCatalogue::clear();
}
