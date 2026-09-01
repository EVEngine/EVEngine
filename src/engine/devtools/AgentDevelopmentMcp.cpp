#include "devtools/AgentDevelopmentMcp.hpp"

#include "devtools/AgentDevelopmentSession.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Stringifier.h>

#include <sstream>
#include <utility>

namespace eve::dev {
namespace {

std::string stringify(const Poco::Dynamic::Var& value) {
    std::ostringstream output;
    Poco::JSON::Stringifier::stringify(value, output, 0, 0);
    return output.str();
}

std::string stringField(Poco::JSON::Object::Ptr object, const char* key) {
    if (!object || !object->has(key)) return {};
    try {
        return object->get(key).convert<std::string>();
    } catch (...) {
        return {};
    }
}

Poco::JSON::Object::Ptr resultObject(const AgentDevelopmentResult& result) {
    Poco::JSON::Object::Ptr output(new Poco::JSON::Object());
    output->set("status", result.code);
    if (!result.message.empty()) output->set("error", result.message);
    return output;
}

Poco::JSON::Object::Ptr snapshot(const AgentDevelopmentResult& result) {
    auto  output  = resultObject(result);
    auto& session = AgentDevelopmentSession::instance();
    output->set("schema", "eve.agent-development-session");
    output->set("schemaVersion", 1);
    output->set("active", session.active());
    output->set("sessionId", session.sessionId());
    output->set("objective", session.objective());
    output->set("phase", std::string(agentDevelopmentPhaseName(session.phase())));
    output->set("readyToComplete", session.readyToComplete());
    if (!session.summary().empty()) output->set("summary", session.summary());

    Poco::JSON::Array::Ptr criteria(new Poco::JSON::Array());
    for (const auto& criterion : session.criteria()) {
        Poco::JSON::Object::Ptr item(new Poco::JSON::Object());
        item->set("id", criterion.id);
        item->set("description", criterion.description);
        item->set("required", criterion.required);
        item->set("passed", criterion.passed);
        criteria->add(item);
    }
    output->set("criteria", criteria);

    Poco::JSON::Array::Ptr evidence(new Poco::JSON::Array());
    for (const auto& receipt : session.evidence()) {
        Poco::JSON::Object::Ptr item(new Poco::JSON::Object());
        item->set("sequence", receipt.sequence);
        item->set("criterionId", receipt.criterionId);
        item->set("kind", receipt.kind);
        item->set("status", receipt.status);
        item->set("summary", receipt.summary);
        if (!receipt.artifact.empty()) item->set("artifact", receipt.artifact);
        evidence->add(item);
    }
    output->set("evidence", evidence);
    return output;
}

AgentDevelopmentResult invalid(std::string code, std::string message) { return {std::move(code), std::move(message)}; }

}  // namespace

bool isAgentDevelopmentTool(std::string_view name) {
    return name == "eve_agent_session_start" || name == "eve_agent_session_advance" ||
           name == "eve_agent_session_evidence" || name == "eve_agent_session_status" ||
           name == "eve_agent_session_complete" || name == "eve_agent_session_abort";
}

std::string callAgentDevelopmentTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    auto&                  session = AgentDevelopmentSession::instance();
    AgentDevelopmentResult result{"applied", {}};
    if (name == "eve_agent_session_start") {
        std::vector<AgentAcceptanceCriterion> criteria;
        try {
            auto array = args ? args->getArray("criteria") : nullptr;
            if (array) {
                for (std::size_t index = 0; index < array->size(); ++index) {
                    auto item = array->getObject(static_cast<unsigned>(index));
                    if (!item) {
                        result = invalid("invalid-criterion", "each criterion must be an object");
                        break;
                    }
                    criteria.push_back({stringField(item, "id"), stringField(item, "description"),
                                        item->optValue<bool>("required", true), false});
                }
            }
        } catch (...) {
            result = invalid("invalid-criteria", "criteria must be an array of objects");
        }
        if (result.isAccepted()) result = session.start(stringField(args, "objective"), std::move(criteria));
    } else if (name == "eve_agent_session_advance") {
        AgentDevelopmentPhase phase = AgentDevelopmentPhase::Aborted;
        result                      = parseAgentDevelopmentPhase(stringField(args, "phase"), &phase);
        if (result.isAccepted()) result = session.advance(stringField(args, "sessionId"), phase);
    } else if (name == "eve_agent_session_evidence") {
        AgentDevelopmentEvidence evidence;
        evidence.criterionId = stringField(args, "criterionId");
        evidence.kind        = stringField(args, "kind");
        evidence.status      = stringField(args, "status");
        evidence.summary     = stringField(args, "summary");
        evidence.artifact    = stringField(args, "artifact");
        result               = session.record(stringField(args, "sessionId"), std::move(evidence));
    } else if (name == "eve_agent_session_complete") {
        result = session.complete(stringField(args, "sessionId"), stringField(args, "summary"));
    } else if (name == "eve_agent_session_abort") {
        result = session.abort(stringField(args, "sessionId"), stringField(args, "reason"));
    } else if (name != "eve_agent_session_status") {
        result = invalid("unknown-tool", "unknown Agent development-session tool");
    }
    return stringify(Poco::Dynamic::Var(snapshot(result)));
}

std::string_view agentDevelopmentToolSchemas() {
    return R"json({"name":"eve_agent_session_start","description":"Start an evidence-driven Agent game-development workflow with explicit acceptance criteria.","inputSchema":{"type":"object","properties":{"objective":{"type":"string"},"criteria":{"type":"array","items":{"type":"object","properties":{"id":{"type":"string"},"description":{"type":"string"},"required":{"type":"boolean"}},"required":["id","description"]}}},"required":["objective","criteria"]}},{"name":"eve_agent_session_advance","description":"Advance the active workflow through discover, modify, run, observe, verify, or recover.","inputSchema":{"type":"object","properties":{"sessionId":{"type":"string"},"phase":{"type":"string","enum":["discover","modify","run","observe","verify","recover"]}},"required":["sessionId","phase"]}},{"name":"eve_agent_session_evidence","description":"Record an immutable evidence receipt against one declared acceptance criterion.","inputSchema":{"type":"object","properties":{"sessionId":{"type":"string"},"criterionId":{"type":"string"},"kind":{"type":"string","enum":["runtime-observation","test","screenshot","checkpoint","artifact","manual"]},"status":{"type":"string","enum":["pass","fail","pending"]},"summary":{"type":"string"},"artifact":{"type":"string"}},"required":["sessionId","criterionId","kind","status","summary"]}},{"name":"eve_agent_session_status","description":"Read the versioned workflow snapshot, criteria, evidence ledger, and completion readiness.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_agent_session_complete","description":"Complete from verify only when every required criterion has passing evidence.","inputSchema":{"type":"object","properties":{"sessionId":{"type":"string"},"summary":{"type":"string"}},"required":["sessionId","summary"]}},{"name":"eve_agent_session_abort","description":"Abort an active workflow with an explicit reason.","inputSchema":{"type":"object","properties":{"sessionId":{"type":"string"},"reason":{"type":"string"}},"required":["sessionId","reason"]}})json";
}

}  // namespace eve::dev
