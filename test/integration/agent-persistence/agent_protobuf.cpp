#include "agent/AgentPersistence.h"

#include "agent_evidence.pb.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

using namespace eve::agent;

TEST_CASE("agent.protobuf.policyRoundTripsAndRejectsFutureFields") {
    Policy policy;
    policy.featureCount = 1;
    policy.actionCount  = 2;
    policy.hiddenWidth  = 1;
    policy.weights.assign(8, 0.25);
    REQUIRE(validatePolicy(policy).ok());

    auto bytes = policyToBinary(policy);
    REQUIRE(bytes.ok());
    auto restored = policyFromBinary(bytes.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().weights, policy.weights);
    CHECK_EQ(policyToBinary(restored.value()).expect("policy re-encode"), bytes.value());

    auto json = policyToJson(policy);
    REQUIRE(json.ok());
    auto fromJson = policyFromJson(json.value());
    REQUIRE(fromJson.ok());
    CHECK_EQ(fromJson.value().weights, policy.weights);
    CHECK(!policyFromJson(R"({"schema":"evengine.agent.policy","version":2,"unknown":1})").ok());

    eve::agent::proto::PolicyDocument future;
    REQUIRE(future.ParseFromString(bytes.value()));
    future.set_version(3);
    CHECK(!policyFromBinary(future.SerializeAsString()).ok());
    future.set_version(2);
    future.GetReflection()->MutableUnknownFields(&future)->AddVarint(99, 1);
    CHECK(!policyFromBinary(future.SerializeAsString()).ok());
    policy.weights[0] = std::numeric_limits<double>::infinity();
    CHECK(!policyToBinary(policy).ok());
}

TEST_CASE("agent.protobuf.traceRoundTripsAndRejectsNestedUnknownFields") {
    Trace trace;
    trace.environmentSeed      = 18446744073709551615ULL;
    trace.dt                   = 1.0 / 60.0;
    trace.initial.features     = {0.0F};
    trace.initial.legalActions = {0, 1};
    Observation terminal;
    terminal.features = {1.0F};
    terminal.outcome  = Outcome::Success;
    terminal.reward   = 1.0;
    terminal.finding  = "done";
    trace.steps.push_back({1, terminal});
    REQUIRE(validateTrace(trace).ok());

    auto bytes = traceToBinary(trace);
    REQUIRE(bytes.ok());
    auto restored = traceFromBinary(bytes.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().environmentSeed, trace.environmentSeed);
    CHECK_EQ(restored.value().steps[0].observation.finding, "done");
    CHECK_EQ(traceToBinary(restored.value()).expect("trace re-encode"), bytes.value());

    auto json = traceToJson(trace);
    REQUIRE(json.ok());
    auto fromJson = traceFromJson(json.value());
    REQUIRE(fromJson.ok());
    CHECK_EQ(fromJson.value().environmentSeed, trace.environmentSeed);

    eve::agent::proto::TraceDocument future;
    REQUIRE(future.ParseFromString(bytes.value()));
    future.mutable_initial()->GetReflection()->MutableUnknownFields(future.mutable_initial())->AddVarint(99, 1);
    CHECK(!traceFromBinary(future.SerializeAsString()).ok());
    future.mutable_initial()->GetReflection()->MutableUnknownFields(future.mutable_initial())->Clear();
    future.mutable_steps(0)->clear_observation();
    CHECK(!traceFromBinary(future.SerializeAsString()).ok());
    trace.steps[0].action = 99;
    CHECK(!traceToBinary(trace).ok());
}
