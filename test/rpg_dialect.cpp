#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgDialect.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace eve::rpg;

/** @brief Borrowed RPG objects one story runs against, plus their owning actor. */
struct StoryFixture {
    GameState                     state;
    Party                         party;
    eve::inventory::Bag           bag{8};
    eve::inventory::EquipmentSet  equipment;
    RPGActor*                     hero = nullptr;

    StoryFixture() {
        hero = RPGActor::createActor();
        party.addMember("hero", hero).ignore("story fixture");
        equipment.defineSlot("weapon");
        hero->setBaseAttribute("attack", 10.0);

        eve::inventory::ItemDefinition potion;
        potion.id         = "potion";
        potion.displayName = "Potion";
        potion.maxStack   = 99;
        potion.tags       = {"consumable"};
        eve::inventory::ItemRegistry::registerItem(potion);

        eve::inventory::ItemDefinition sword;
        sword.id         = "iron-sword";
        sword.displayName = "Iron Sword";
        sword.maxStack   = 1;
        sword.equipSlot  = "weapon";
        sword.tags       = {"weapon"};
        eve::inventory::ItemRegistry::registerItem(sword);

        // The `equipment` step equips *from the bag*, so the sword starts there.
        bag.addItem("iron-sword", 1);
    }

    ~StoryFixture() {
        eve::inventory::ItemRegistry::clear();
        if (hero) hero->release();
    }

    StoryFixture(const StoryFixture&)            = delete;
    StoryFixture& operator=(const StoryFixture&) = delete;

    RpgStoryBinding binding() {
        RpgStoryBinding value;
        value.gameState = &state;
        value.party     = &party;
        value.bag       = &bag;
        value.equipment = &equipment;
        return value;
    }
};

/**
 * @brief Reference movement port: a tiny continuous-space world.
 *
 * It deliberately has no grid, no path graph and no tile size. The point of the
 * port contract is that a tile RPG, a tactics board and a continuous world
 * satisfy it identically, so the engine never has to pick one — and a test can
 * prove it by using fractional coordinates a grid model could not represent.
 */
class FakeMoveWorld {
public:
    struct Position {
        double x = 0.0;
        double y = 0.0;
    };

    eve::Result<StoryMoveStatus> begin(const StoryMoveRequest& request) {
        requests.push_back(request);
        if (refuse)
            return eve::Result<StoryMoveStatus>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "fake world: the movement service is offline", "move", {}, "test"));
        if (!reachable) return eve::Result<StoryMoveStatus>::success(StoryMoveStatus::Unreachable);
        const Position& current = positions[request.actorId];
        if (current.x == request.x && current.y == request.y)
            return eve::Result<StoryMoveStatus>::success(StoryMoveStatus::Arrived);
        pending[request.actorId] = {request.x, request.y};
        return eve::Result<StoryMoveStatus>::success(StoryMoveStatus::Travelling);
    }

    StoryMoveHandler handler() {
        return [this](const StoryMoveRequest& request) { return begin(request); };
    }

    /** @brief Complete every in-flight move, the way a host update loop would. */
    void finishAll() {
        for (const auto& entry : pending) positions[entry.first] = entry.second;
        pending.clear();
    }

    [[nodiscard]] bool isTravelling(const std::string& actorId) const { return pending.contains(actorId); }

    [[nodiscard]] Position positionOf(const std::string& actorId) const {
        const auto found = positions.find(actorId);
        return found == positions.end() ? Position{} : found->second;
    }

    bool                            reachable = true;
    bool                            refuse    = false;
    std::vector<StoryMoveRequest>   requests;
    std::map<std::string, Position> positions;
    std::map<std::string, Position> pending;
};

/**
 * @brief Reference animation port: a tiny playback stage with no rig at all.
 *
 * It deliberately has no skeleton, no clip registry and no clip format: `clip` is
 * just an opaque string it echoes back. The point of the port contract is that a
 * skeletal stack, a sprite sequence and a Spine setup satisfy it identically, so
 * the engine never has to pick one — and a test can prove it by playing a clip id
 * that no importer could ever resolve.
 *
 * `instant` models the other end of the contract: a host whose non-looping
 * playback is already over when the call returns. A looping clip always reports
 * `Playing`, because a clip with no end has no end for the host to report.
 */
class FakeAnimStage {
public:
    eve::Result<StoryAnimationStatus> begin(const StoryAnimationRequest& request) {
        requests.push_back(request);
        if (refuse)
            return eve::Result<StoryAnimationStatus>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "fake stage: the clip library is still streaming",
                                       "animation", {}, "test"));
        if (!available) return eve::Result<StoryAnimationStatus>::success(StoryAnimationStatus::Unavailable);
        if (!request.loop && instant) return eve::Result<StoryAnimationStatus>::success(StoryAnimationStatus::Finished);
        playing[request.targetId] = request.clip;
        return eve::Result<StoryAnimationStatus>::success(StoryAnimationStatus::Playing);
    }

    StoryAnimationHandler handler() {
        return [this](const StoryAnimationRequest& request) { return begin(request); };
    }

    /** @brief Finish every in-flight clip, the way a host update loop would. */
    void finishAll() { playing.clear(); }

    [[nodiscard]] bool isPlaying(const std::string& targetId) const { return playing.contains(targetId); }

    bool                               available = true;
    bool                               refuse    = false;
    bool                               instant   = false;
    std::vector<StoryAnimationRequest> requests;
    std::map<std::string, std::string> playing;
};

}  // namespace

TEST_CASE("rpg.dnut.runsDomainAndPresentationSteps") {
    StoryFixture fixture;
    RpgStoryBinding binding = fixture.binding();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.arrival {
    move actor=hero x=10 y=4 duration=0.5
    skill actor=hero learn=fireball
    attribute actor=hero name=attack op=add value=5
    item add=potion count=2
    equipment actor=hero equip=iron-sword slot=weapon
    flag name=village.visited state=true
    variable name=gold op=add value=25
    message text=Arrived
}
)",
                                                     "village.dnut")
                .ok());
    CHECK_EQ(RpgStoryCatalogue::count(), 1);
    CHECK(RpgStoryCatalogue::contains("village.arrival"));

    RpgStorySession session;
    REQUIRE(session.begin("village.arrival", &binding).ok());
    CHECK(session.isActive());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));
    CHECK_EQ(session.getStepPayload().find("actor")->asString(), std::string("hero"));

    // Presentation step: the domain effects have not run yet.
    CHECK(!fixture.hero->knowsSkill("fireball"));

    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK(fixture.hero->knowsSkill("fireball"));
    CHECK_EQ(fixture.hero->getBaseAttribute("attack"), 15.0);
    CHECK_EQ(fixture.bag.countItem("potion"), 2);
    CHECK_EQ(fixture.equipment.getSlotItemId("weapon"), std::string("iron-sword"));
    CHECK(fixture.state.isSwitchOn("village.visited"));
    CHECK_EQ(fixture.state.getVariable("gold"), 25.0);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());
    CHECK(fixture.state.hasSelfVariable("story.village.arrival", "completed"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.rejectsInvalidStepPayloads") {
    RpgStoryCatalogue::clear();
    const auto rejected = [](const std::string& body) {
        return !RpgStoryCatalogue::replaceFromDnutStrict("story probe {\n" + body + "\n}\n", "probe.dnut").ok();
    };

    CHECK(rejected("    teleport actor=hero"));
    CHECK(rejected("    skill actor=hero"));
    CHECK(rejected("    skill actor=hero learn=fireball forget=fireball"));
    CHECK(rejected("    attribute actor=hero name=attack op=bogus value=1"));
    CHECK(rejected("    attribute actor=hero name=attack op=add"));
    CHECK(rejected("    equipment actor=hero equip=iron-sword"));
    CHECK(rejected("    item add=potion remove=potion"));
    CHECK(rejected("    move actor=hero x=1 y=2 z=3"));
    CHECK(rejected("    variable name=gold op=multiply value=2"));
    CHECK(rejected("    if switch.gate == true {\n        teleport\n    }"));

    // A document without any story block must not silently wipe the catalogue.
    CHECK(!RpgStoryCatalogue::replaceFromDnutStrict("pool village {\n    elder: \"hi\"\n}\n", "pool.dnut").ok());
    CHECK_EQ(RpgStoryCatalogue::count(), 0);
}

TEST_CASE("rpg.dnut.gatesNonRepeatableStoriesOnCompletion") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story once {
    variable name=once op=set value=1
}
story again repeatable {
    variable name=again op=add value=1
}
)",
                                                     "gates.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("once", &binding).ok());
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("once"), 1.0);
    auto repeat = session.begin("once", &binding);
    CHECK(!repeat.ok());
    const auto* repeatDiagnostic = repeat.error();
    REQUIRE(repeatDiagnostic != nullptr);
    CHECK(repeatDiagnostic->message().find("already complete") != std::string::npos);

    REQUIRE(session.begin("again", &binding).ok());
    REQUIRE(session.begin("again", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("again"), 2.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.branchesOnGameState") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story gate.check repeatable {
    if switch.gate == true {
        variable name=path op=set value=1
    } else {
        variable name=path op=set value=2
    }
}
)",
                                                     "gate.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("gate.check", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("path"), 2.0);

    fixture.state.switchOn("gate");
    REQUIRE(session.begin("gate.check", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("path"), 1.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.selectsChoiceRoutes") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story shop.buy repeatable {
    choice {
        option "Buy" when variable.gold >= 10 {
            variable name=bought op=set value=1
        }
        option "Leave" {
            variable name=bought op=set value=0
        }
    }
}
)",
                                                     "shop.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("shop.buy", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("choice"));
    REQUIRE_EQ(session.getChoiceLabels().size(), 2u);
    CHECK_EQ(session.getChoiceLabels()[0], std::string("Buy"));

    // The conditional route is rejected while the state does not satisfy it.
    CHECK(!session.select("Buy").ok());
    CHECK(session.isBlocked());
    CHECK(!session.select("Nothing").ok());

    REQUIRE(session.select("Leave").ok());
    CHECK_EQ(fixture.state.getVariable("bought"), 0.0);

    fixture.state.setVariable("gold", 20.0);
    REQUIRE(session.begin("shop.buy", &binding).ok());
    REQUIRE(session.select("Buy").ok());
    CHECK_EQ(fixture.state.getVariable("bought"), 1.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.reportFailureWhenDomainStateIsMissing") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story broke {
    item remove=potion count=3
}
)",
                                                     "broke.dnut")
                .ok());

    RpgStorySession session;
    auto             start = session.begin("broke", &binding);
    CHECK(!start.ok());
    CHECK(!session.isActive());
    const auto* diagnostic = start.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("does not hold") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.capturesAndRestoresCursor") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story resume.me {
    variable name=step op=set value=1
    message text=pause
    variable name=step op=set value=2
}
)",
                                                     "resume.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("resume.me", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("resume.me", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("message"));
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 2.0);
    CHECK(!restored.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.drivesActorMovementThroughTheHostController") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.walk {
    move actor=hero x=10 y=4 duration=0.5
    message text=Arrived
}
)",
                                                     "walk.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.walk", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));

    // Effect verification: the controller received exactly this move, and the
    // actor has not moved yet because the move is still travelling.
    REQUIRE_EQ(world.requests.size(), 1u);
    const StoryMoveRequest& request = world.requests.front();
    CHECK_EQ(request.actorId, std::string("hero"));
    CHECK_EQ(request.x, 10.0);
    CHECK_EQ(request.y, 4.0);
    CHECK(request.hasDuration);
    CHECK_EQ(request.duration, 0.5);
    CHECK(request.actor == fixture.hero);
    CHECK(world.isTravelling("hero"));
    CHECK_EQ(world.positionOf("hero").x, 0.0);
    CHECK_EQ(world.positionOf("hero").y, 0.0);

    // The story stays on the move step until the host reports the arrival.
    world.finishAll();
    REQUIRE(session.advance().ok());
    CHECK_EQ(world.positionOf("hero").x, 10.0);
    CHECK_EQ(world.positionOf("hero").y, 4.0);
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // A move is dispatched once, not once per acknowledgement.
    CHECK_EQ(world.requests.size(), 1u);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.completesMoveInlineWhenTheActorIsAlreadyThere") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    // Fractional coordinates: the engine must not snap them to a grid.
    world.positions["hero"] = {2.5, 7.25};
    binding.moveActor       = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.snap {
    move actor=hero x=2.5 y=7.25
    variable name=step op=set value=1
}
)",
                                                     "snap.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.snap", &binding).ok());
    // `Arrived` completes the step inline, so the story never suspends on it.
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    REQUIRE_EQ(world.requests.size(), 1u);
    CHECK_EQ(world.requests.front().x, 2.5);
    CHECK_EQ(world.requests.front().y, 7.25);
    CHECK(!world.requests.front().hasDuration);
    CHECK(!world.isTravelling("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.failsMoveWhenTheDestinationIsUnreachable") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    world.reachable   = false;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.blocked {
    move actor=hero x=99 y=99
}
)",
                                                     "blocked.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.blocked", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto* diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("cannot reach") != std::string::npos);
    CHECK(diagnostic->message().find("99") != std::string::npos);
    CHECK(!world.isTravelling("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.propagatesMovementControllerFailure") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    world.refuse      = true;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.offline {
    move actor=hero x=1 y=1
}
)",
                                                     "offline.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.offline", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto* diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("movement service is offline") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.presentsMoveWhenNoControllerIsBound") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.manual {
    move actor=hero x=10 y=4 duration=0.5
    message text=Arrived
}
)",
                                                     "manual.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.manual", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));
    CHECK(!binding.moveActor);

    // The host presentation path is unchanged: read the payload and advance.
    CHECK_EQ(session.getStepPayload().find("actor")->asString(), std::string("hero"));
    CHECK_EQ(session.getStepPayload().find("x")->asInt(), std::int64_t(10));
    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // Nothing reached a movement controller, because none was installed.
    CHECK(world.requests.empty());

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.movesSubjectsOutsideThePartyRoster") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.pan {
    move actor=camera.main x=-2.5 y=7.25
}
)",
                                                     "pan.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.pan", &binding).ok());

    // The subject is not a party member, so the convenience projection is null —
    // but the step still runs, because the host owns the subject space.
    REQUIRE_EQ(world.requests.size(), 1u);
    CHECK_EQ(world.requests.front().actorId, std::string("camera.main"));
    CHECK(world.requests.front().actor == nullptr);
    CHECK_EQ(world.requests.front().x, -2.5);
    CHECK_EQ(world.requests.front().y, 7.25);
    CHECK(world.isTravelling("camera.main"));

    world.finishAll();
    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.keepsTravellingMoveAcrossCaptureAndRestore") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.longwalk {
    move actor=hero x=6 y=2
    variable name=step op=set value=1
}
)",
                                                     "longwalk.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.longwalk", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("move"));

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("village.longwalk", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("move"));

    // The restored session does not re-dispatch the move; the host finishes it.
    world.finishAll();
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    CHECK(!restored.isActive());
    CHECK_EQ(world.positionOf("hero").x, 6.0);
    CHECK_EQ(world.requests.size(), 1u);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.drivesAnimationThroughTheHostController") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.cheer {
    animation target=hero clip=emote.cheer loop=true hold=true
    message text=Cheered
}
)",
                                                     "cheer.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.cheer", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("animation"));

    // Effect verification: the controller received exactly this request, and the
    // clip has not ended because it is still playing.
    REQUIRE_EQ(stage.requests.size(), 1u);
    const StoryAnimationRequest& request = stage.requests.front();
    CHECK_EQ(request.targetId, std::string("hero"));
    CHECK_EQ(request.clip, std::string("emote.cheer"));
    CHECK(request.loop);
    CHECK(request.hold);
    CHECK(request.actor == fixture.hero);
    CHECK(stage.isPlaying("hero"));

    // The story stays on the animation step until the host reports the end.
    stage.finishAll();
    REQUIRE(session.advance().ok());
    CHECK(!stage.isPlaying("hero"));
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // A clip is dispatched once, not once per acknowledgement.
    CHECK_EQ(stage.requests.size(), 1u);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.completesAnimationInlineWhenTheHostFinishesImmediately") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    // A host whose playback is instantaneous completes the step without a loop.
    stage.instant         = true;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.blink {
    animation target=hero clip=emote.blink
    variable name=step op=set value=1
}
)",
                                                     "blink.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.blink", &binding).ok());
    // `Finished` completes the step inline, so the story never suspends on it.
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    REQUIRE_EQ(stage.requests.size(), 1u);
    CHECK_EQ(stage.requests.front().clip, std::string("emote.blink"));
    // Omitted flags default to false rather than to a host-specific guess.
    CHECK(!stage.requests.front().loop);
    CHECK(!stage.requests.front().hold);
    CHECK(!stage.isPlaying("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.failsAnimationWhenTheClipIsUnavailable") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    stage.available       = false;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.missing {
    animation target=hero clip=emote.missing
}
)",
                                                     "missing.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.missing", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto* diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("cannot play clip") != std::string::npos);
    CHECK(diagnostic->message().find("emote.missing") != std::string::npos);
    CHECK(diagnostic->message().find("hero") != std::string::npos);
    CHECK(!stage.isPlaying("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.propagatesAnimationControllerFailure") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    stage.refuse          = true;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.streaming {
    animation target=hero clip=emote.cheer
}
)",
                                                     "streaming.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.streaming", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto* diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("clip library is still streaming") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.presentsAnimationWhenNoControllerIsBound") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.manualclip {
    animation target=hero clip=emote.wave
    message text=Done
}
)",
                                                     "manualclip.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.manualclip", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("animation"));
    CHECK(!binding.playAnimation);

    // The host presentation path is unchanged: read the payload and advance.
    CHECK_EQ(session.getStepPayload().find("target")->asString(), std::string("hero"));
    CHECK_EQ(session.getStepPayload().find("clip")->asString(), std::string("emote.wave"));
    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // Nothing reached a playback controller, because none was installed.
    CHECK(stage.requests.empty());

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.animatesSubjectsOutsideThePartyRoster") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.flash {
    animation target=fx.torch clip=fx.flicker loop=true
}
)",
                                                     "flash.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.flash", &binding).ok());

    // The subject is not a party member, so the convenience projection is null —
    // but the step still runs, because the host owns the subject space.
    REQUIRE_EQ(stage.requests.size(), 1u);
    CHECK_EQ(stage.requests.front().targetId, std::string("fx.torch"));
    CHECK(stage.requests.front().actor == nullptr);
    CHECK_EQ(stage.requests.front().clip, std::string("fx.flicker"));
    CHECK(stage.requests.front().loop);
    CHECK(stage.isPlaying("fx.torch"));

    stage.finishAll();
    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.keepsPlayingAnimationAcrossCaptureAndRestore") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.emote {
    animation target=hero clip=emote.dance
    variable name=step op=set value=1
}
)",
                                                     "emote.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.emote", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("animation"));

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("village.emote", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("animation"));

    // The restored session does not restart the clip; the host finishes it.
    stage.finishAll();
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    CHECK(!restored.isActive());
    CHECK(!stage.isPlaying("hero"));
    CHECK_EQ(stage.requests.size(), 1u);

    RpgStoryCatalogue::clear();
}
