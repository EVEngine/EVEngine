#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/AgentDevelopmentSession.hpp"
#include "devtools/AgentDevelopmentMcp.hpp"

#include <Poco/JSON/Parser.h>

using namespace eve::dev;

TEST_CASE("devtools.agentDevelopmentSession.enforcesWorkflowAndEvidenceGate") {
    auto& session = AgentDevelopmentSession::instance();
    session.reset();

    std::vector<AgentAcceptanceCriterion> criteria{
        {"runtime", "Runtime state matches the requested behavior", true, false},
        {"visual", "Rendered output is legible", true, false},
        {"note", "Optional design note", false, false},
    };
    auto started = session.start("Build one verified game feature", std::move(criteria));
    REQUIRE(started.isAccepted());
    CHECK_EQ(agentDevelopmentPhaseName(session.phase()), std::string_view("discover"));
    CHECK(!session.readyToComplete());
    const std::string id = session.sessionId();

    CHECK_EQ(session.advance(id, AgentDevelopmentPhase::Run).code, "invalid-transition");
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Observe).isAccepted());
    REQUIRE(session.record(id, {0, "visual", "manual", "pass", "looks fine", {}}).isAccepted());
    CHECK(!session.criteria()[1].passed);
    REQUIRE(session.record(id, {0, "runtime", "runtime-observation", "pass", "x reached 4", {}}).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Verify).isAccepted());
    CHECK_EQ(session.complete(id, "premature").code, "acceptance-incomplete");

    REQUIRE(session.record(id, {0, "visual", "screenshot", "pass", "frame inspected", "frame.png"}).isAccepted());
    CHECK(session.readyToComplete());
    REQUIRE(session.complete(id, "feature verified").isAccepted());
    CHECK_EQ(agentDevelopmentPhaseName(session.phase()), std::string_view("complete"));
    CHECK(!session.active());
    CHECK_EQ(session.evidence().size(), std::size_t{3});
}

TEST_CASE("devtools.agentDevelopmentSession.rejectsEvalAndRequiresScreenshotForVisual") {
    auto& session = AgentDevelopmentSession::instance();
    session.reset();
    REQUIRE(session.start("Verify without eval",
                          {{"visual", "Must see the frame", true, false},
                           {"runtime", "State moved", true, false}})
                .isAccepted());
    const std::string id = session.sessionId();
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Observe).isAccepted());
    CHECK_EQ(session.record(id, {0, "runtime", "eval", "pass", "eve_eval cheated", {}}).code,
             "invalid-evidence-kind");
    REQUIRE(session.record(id, {0, "runtime", "runtime-observation", "pass", "tick moved", {}}).isAccepted());
    REQUIRE(session.record(id, {0, "visual", "runtime-observation", "pass", "looked at numbers", {}}).isAccepted());
    CHECK(!session.criteria()[0].passed);
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Verify).isAccepted());
    CHECK_EQ(session.complete(id, "missing screenshot").code, "acceptance-incomplete");
    REQUIRE(session.record(id, {0, "visual", "screenshot", "pass", "frame inspected", "frame.png"}).isAccepted());
    REQUIRE(session.record(id, {0, "runtime", "play-trace", "pass", "trace stable", "trace.json"}).isAccepted());
    REQUIRE(session.complete(id, "verified without eval").isAccepted());
}

TEST_CASE("devtools.agentDevelopmentSession.recoversAndAbortsExplicitly") {
    auto& session = AgentDevelopmentSession::instance();
    session.reset();
    REQUIRE(session.start("Recover a broken game", {{"smoke", "Smoke passes", true, false}}).isAccepted());
    const std::string id = session.sessionId();
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Recover).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.abort(id, "dependency unavailable").isAccepted());
    CHECK_EQ(agentDevelopmentPhaseName(session.phase()), std::string_view("aborted"));
    CHECK_EQ(session.summary(), "dependency unavailable");
    CHECK_EQ(session.advance(id, AgentDevelopmentPhase::Run).code, "session-terminal");
}

TEST_CASE("devtools.agentDevelopmentSession.mcpAdapterReturnsVersionedSnapshot") {
    AgentDevelopmentSession::instance().reset();
    Poco::JSON::Parser parser;
    auto args = parser
                    .parse(R"({"objective":"Ship a playable loop","criteria":[{"id":"play","description":"Loop is playable"}]})")
                    .extract<Poco::JSON::Object::Ptr>();
    Poco::JSON::Parser responseParser;
    auto response = responseParser.parse(callAgentDevelopmentTool("eve_agent_session_start", args))
                        .extract<Poco::JSON::Object::Ptr>();
    REQUIRE(response);
    CHECK_EQ(response->getValue<std::string>("status"), "applied");
    CHECK_EQ(response->getValue<std::string>("schema"), "eve.agent-development-session");
    CHECK_EQ(response->getValue<int>("schemaVersion"), 1);
    CHECK_EQ(response->getValue<std::string>("phase"), "discover");
    CHECK_EQ(response->getArray("criteria")->size(), std::size_t{1});
}
