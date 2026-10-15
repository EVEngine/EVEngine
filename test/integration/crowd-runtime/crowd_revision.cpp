#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

using namespace eve::crowd;

TEST_CASE("crowd.revision.resizeRejectsInvalidGeometryWithoutPublishingState") {
    CrowdField field;
    field.resize(8, 8, 1.f, 0.f, 0.f);
    field.setGoal(7, 0);
    field.build();
    const auto revision = field.revision();
    field.resize(8, 8, 1.f, 0.f, 0.f);
    CHECK_EQ(field.revision(), revision);
    CHECK(field.isBuilt());
    bool rejected = false;
    try {
        field.resize(8, 8, 1.f, std::numeric_limits<float>::quiet_NaN(), 0.f);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    rejected = false;
    try {
        field.resize(65536, 65536, 1.f, 0.f, 0.f);
    } catch (const std::length_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
    CHECK_EQ(field.revision(), revision);
    CHECK_EQ(field.getWidth(), 8);
    CHECK(field.isBuilt());
    CHECK(field.isReachable(0, 0));
}

TEST_CASE("crowd.revision.scriptStatusAndStepReportExposeStaleField") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        assert(crowd.getFieldStatus().value.state == "unconfigured");
        crowd.resizeField(8,8,1.0,0.0,0.0);
        assert(crowd.getFieldStatus().value.state == "unbuilt");
        crowd.buildFlowField(7,0);
        local built = crowd.getFieldStatus().value;
        assert(built.state == "ready");
        assert(built.revision == built.builtRevision);
        crowd.setBlocked(4,4,true);
        local stale = crowd.getFieldStatus().value;
        assert(stale.state == "stale");
        assert(stale.revision != stale.builtRevision);
        assert(typeof stale.revision == "string");
        local report = crowd.advanceTick({tick=1,deltaNanoseconds=10000000});
        assert(report.ok);
        assert(report.value.navigationState == 3);
        assert(report.value.navigationRevision == stale.revision);
        assert(crowd.advanceTick({tick=1,deltaNanoseconds=0}).value.navigationState == 3);
        crowd.buildFlowField(7,0);
        assert(crowd.getFieldStatus().value.state == "ready");
    )"));
}

TEST_CASE("crowd.revision.terrainAndGoalsInvalidateSamplingUntilRebuild") {
    CrowdField field;
    field.resize(8, 8, 1.f, 0.f, 0.f);
    CHECK(field.state() == FieldState::Unbuilt);
    field.setGoal(7, 0);
    field.build();
    CHECK(field.state() == FieldState::Ready);
    const auto built = field.builtRevision();
    field.setGoal(7, 0);
    field.addGoal(7, 0);
    CHECK_EQ(field.revision(), built);
    CHECK(field.state() == FieldState::Ready);
    float dx = 0.f, dy = 0.f;
    field.flowAtCell(0, 0, dx, dy);
    CHECK(dx > 0.f);
    field.setBlocked(4, 4, true);
    CHECK(field.state() == FieldState::Stale);
    CHECK(field.revision() > built);
    field.flowAtCell(0, 0, dx, dy);
    CHECK_EQ(dx, 0.f);
    CHECK_EQ(dy, 0.f);
    CHECK_EQ(field.costAtWorld(0.5f, 0.5f), CrowdField::kUnreachable);
    const auto revision = field.revision();
    field.setBlocked(4, 4, true);
    CHECK_EQ(field.revision(), revision);
    field.build();
    CHECK(field.isBuilt());
    CHECK_EQ(field.builtRevision(), field.revision());
    field.addGoal(0, 7);
    CHECK(field.state() == FieldState::Stale);
    field.flowAtWorld(0.5f, 0.5f, dx, dy);
    CHECK_EQ(dx, 0.f);
    CHECK_EQ(dy, 0.f);
}

TEST_CASE("crowd.revision.staleFlowStopsDrivingButLatestWallStillConstrains") {
    Crowd crowd;
    crowd.resizeField(8, 8, 1.f, 0.f, 0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.setSeparationWeight(0.f);
    crowd.buildFlowField(7, 0);
    const int agent = crowd.addAgent(0.5f, 0.5f, 0.f, 0.1f);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.getAgentState(agent).vx > 0.f);
    crowd.setBlocked(4, 4, true);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.getAgentState(agent).vx, 0.f);
    REQUIRE(crowd.setAgentPosition(agent, 4.5f, 4.5f));
    REQUIRE(crowd.advance(0.01f).ok());
    const auto state = crowd.getAgentState(agent);
    CHECK((state.x < 4.f || state.x > 5.f || state.y < 4.f || state.y > 5.f));
}
