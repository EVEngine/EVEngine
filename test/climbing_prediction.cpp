#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <memory>

namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

eve::climbing::ClimbingActionDefinition mantleAction() {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = "core:mantle";
    action.kind           = eve::climbing::ClimbingActionKind::Mantle;
    action.minHeight      = 0.4f;
    action.maxHeight      = 1.2f;
    action.duration       = seconds(0.6);
    action.landingForward = 0.6f;
    action.apexHeight     = 0.7f;
    return action;
}

struct Fixture {
    Fixture() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        shape    = obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    eve::climbing::ClimbingPose pose() const { return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1, 0.f, true}; }

    eve::climbing::ClimbingPredictionRequest requestFor(eve::climbing::ClimbingRuntime& runtime) const {
        auto candidates = runtime.probe(*world, pose());
        REQUIRE(candidates.ok());
        REQUIRE_EQ(candidates.value().size(), std::size_t(1));
        eve::climbing::ClimbingPredictionRequest request;
        request.sequence   = eve::climbing::ClimbingPredictionSequence(7);
        request.clientTick = eve::SimulationTick(100);
        request.candidate  = eve::climbing::makeClimbingCandidateKey(candidates.value().front(), 0);
        return request;
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  obstacle = nullptr;
    eve::physics::Shape3D*                 shape    = nullptr;
};

}  // namespace

TEST_CASE("climbing.prediction.acceptsOnlyServerReprobedExactCandidate") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request = fixture.requestFor(runtime);

    auto decision = runtime.tryBeginPredicted(*fixture.world, fixture.pose(), request, eve::SimulationTick(100), 2);
    REQUIRE(decision.ok());
    CHECK_EQ(static_cast<int>(decision.value().disposition),
             static_cast<int>(eve::climbing::ClimbingPredictionDisposition::Accepted));
    CHECK_EQ(static_cast<int>(decision.value().reason),
             static_cast<int>(eve::climbing::ClimbingPredictionReason::None));
    CHECK(decision.value().authoritativeCandidate == request.candidate);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Requested));

    auto encoded = eve::climbing::encodeClimbingPredictionDecision(decision.value());
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingPredictionDecision(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().sequence.value(), std::uint64_t(7));
    CHECK(decoded.value().authoritativeCandidate == request.candidate);
}

TEST_CASE("climbing.prediction.mismatchCorrectsWithoutStartingClientTarget") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request = fixture.requestFor(runtime);
    request.candidate.fingerprint ^= 0x55u;

    auto decision = runtime.tryBeginPredicted(*fixture.world, fixture.pose(), request, eve::SimulationTick(100), 2);
    REQUIRE(decision.ok());
    CHECK_EQ(static_cast<int>(decision.value().disposition),
             static_cast<int>(eve::climbing::ClimbingPredictionDisposition::Corrected));
    CHECK_EQ(static_cast<int>(decision.value().reason),
             static_cast<int>(eve::climbing::ClimbingPredictionReason::CandidateMismatch));
    CHECK(decision.value().authoritativeCandidate.fingerprint != request.candidate.fingerprint);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
}

TEST_CASE("climbing.prediction.tickLeadAndWireSchemaAreBounded") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request                            = fixture.requestFor(runtime);
    request.extensionMetadata["futureHint"] = eve::Value(std::string("keep-me"));

    auto encoded = eve::climbing::encodeClimbingPredictionRequest(request);
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingPredictionRequest(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().candidate == request.candidate);
    CHECK(decoded.value().extensionMetadata.contains("futureHint"));

    decoded.value().clientTick = eve::SimulationTick(110);
    auto rejected =
        runtime.tryBeginPredicted(*fixture.world, fixture.pose(), decoded.value(), eve::SimulationTick(100), 2);
    REQUIRE(rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.value().disposition),
             static_cast<int>(eve::climbing::ClimbingPredictionDisposition::Rejected));
    CHECK_EQ(static_cast<int>(rejected.value().reason),
             static_cast<int>(eve::climbing::ClimbingPredictionReason::TickTooFarAhead));
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));

    auto object             = *encoded.value().getIf<eve::Value::Object>();
    object["schemaVersion"] = eve::Value(std::int64_t(2));
    auto future             = eve::climbing::decodeClimbingPredictionRequest(eve::Value(std::move(object)));
    CHECK(!future.ok());
    REQUIRE(future.error() != nullptr);
    CHECK_EQ(static_cast<int>(future.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}

TEST_CASE("climbing.prediction.decisionRejectsMalformedOrFutureRuntimeSnapshot") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request  = fixture.requestFor(runtime);
    auto accepted = runtime.tryBeginPredicted(*fixture.world, fixture.pose(), request, eve::SimulationTick(100), 2);
    REQUIRE(accepted.ok());
    auto encoded = eve::climbing::encodeClimbingPredictionDecision(accepted.value());
    REQUIRE(encoded.ok());

    auto malformed = encoded.value();
    malformed.set("authoritativeSnapshot",
                  eve::Value::object({{"schemaId", "wrong"}, {"schemaVersion", std::int64_t(1)}}));
    auto malformedResult = eve::climbing::decodeClimbingPredictionDecision(malformed);
    CHECK(!malformedResult.ok());
    REQUIRE(malformedResult.error() != nullptr);
    CHECK_EQ(static_cast<int>(malformedResult.error()->code()), static_cast<int>(eve::DiagnosticCode::ParseError));

    auto future               = encoded.value();
    auto snapshot             = *future.find("authoritativeSnapshot")->getIf<eve::Value::Object>();
    snapshot["schemaVersion"] = eve::Value(eve::climbing::ClimbingRuntime::SnapshotSchemaVersion + 1);
    future.set("authoritativeSnapshot", eve::Value(std::move(snapshot)));
    auto futureResult = eve::climbing::decodeClimbingPredictionDecision(future);
    CHECK(!futureResult.ok());
    REQUIRE(futureResult.error() != nullptr);
    CHECK_EQ(static_cast<int>(futureResult.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}
