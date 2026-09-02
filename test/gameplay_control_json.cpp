#include "common/Capability.h"
#include "common/GameplayControlJson.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "devtools/DevTool.hpp"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId logical(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

class JsonProvider final : public eve::IGameplayControlProvider {
public:
    explicit JsonProvider(eve::SubjectRef instance) : instance_(instance) {
        eve::cap::addListener<eve::IGameplayControlProvider>(this);
    }
    ~JsonProvider() override { eve::cap::removeListener<eve::IGameplayControlProvider>(this); }

    [[nodiscard]] std::string_view gameplayDomain() const noexcept override { return "json-fixture"; }
    [[nodiscard]] eve::Result<eve::GameplayObservation> observeGameplay(
        const eve::GameplaySession&, eve::SubjectRef instance) const override {
        if (instance != instance_)
            return eve::Result<eve::GameplayObservation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "fixture not found", "instance"));
        eve::GameplayObservation result;
        result.domain = logical("gameplay:json-fixture");
        result.instance = instance_;
        result.tick = tick_;
        result.revision = revision_;
        result.state = eve::Value(eve::Value::Object{{"count", eve::Value(count_)}});
        return eve::Result<eve::GameplayObservation>::success(std::move(result));
    }
    [[nodiscard]] eve::Result<std::vector<eve::GameplayActionDescriptor>> availableGameplayActions(
        const eve::GameplaySession&, eve::SubjectRef, eve::SubjectRef) const override {
        return eve::Result<std::vector<eve::GameplayActionDescriptor>>::success(
            {{logical("fixture:increment"), eve::Value(eve::Value::Object{})}});
    }
    [[nodiscard]] eve::Result<eve::GameplayCommandReceipt> submitGameplay(
        const eve::GameplaySession&, eve::SubjectRef, const eve::GameplayCommand& command) override {
        ++count_;
        ++revision_;
        eve::GameplayCommandReceipt result;
        result.commandId = command.id;
        result.executionId = "fixture-execution";
        result.acceptedTick = tick_;
        result.resultingRevision = revision_;
        result.details = eve::Value(eve::Value::Object{{"count", eve::Value(count_)}});
        return eve::Result<eve::GameplayCommandReceipt>::success(std::move(result));
    }
    [[nodiscard]] eve::Result<eve::GameplayObservation> advanceGameplay(
        const eve::GameplaySession& session, eve::SubjectRef instance,
        const eve::SimulationStep& step) override {
        tick_ = step.tick;
        return observeGameplay(session, instance);
    }
    [[nodiscard]] eve::Result<std::vector<eve::GameplayEvent>> gameplayEvents(
        const eve::GameplaySession&, eve::SubjectRef, std::uint64_t) const override {
        return eve::Result<std::vector<eve::GameplayEvent>>::success({});
    }

private:
    eve::SubjectRef instance_;
    eve::SimulationTick tick_ = eve::SimulationTick::zero();
    std::uint64_t revision_ = 1;
    int count_ = 0;
};

std::string envelope(const std::string& operation, const std::string& instance,
                     const std::string& extra = {}) {
    return "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,"
           "\"op\":\"" + operation + "\",\"domain\":\"json-fixture\",\"instance\":\"" + instance +
           "\",\"session\":{\"id\":\"test\",\"access\":\"player\",\"controlledSubjects\":[\"" +
           instance + "\"]}" + extra + "}";
}

}  // namespace

TEST_CASE("gameplay.control.jsonFacadeRoutesObserveActionsSubmitAdvanceAndEvents") {
    const auto instance = subject("00000000-0000-7000-8000-000000000a01");
    JsonProvider provider(instance);
    const std::string instanceText = instance.format();

    auto domains = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains"})");
    REQUIRE(domains.ok());
    CHECK(domains.value().find("json-fixture") != std::string::npos);

    auto observed = eve::executeGameplayControlJson(envelope("observe", instanceText));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("\"revision\":1") != std::string::npos);

    auto actions = eve::executeGameplayControlJson(
        envelope("actions", instanceText, ",\"subject\":\"" + instanceText + "\""));
    REQUIRE(actions.ok());
    CHECK(actions.value().find("fixture:increment") != std::string::npos);

    const std::string command =
        ",\"command\":{\"id\":\"json-command-1\",\"action\":\"fixture:increment\","
        "\"subject\":\"" + instanceText +
        "\",\"observedTick\":0,\"expectedRevision\":1,\"parameters\":{}}";
    auto submitted = eve::executeGameplayControlJson(envelope("submit", instanceText, command));
    REQUIRE(submitted.ok());
    CHECK(submitted.value().find("json-command-1") != std::string::npos);
    CHECK(submitted.value().find("\"resultingRevision\":2") != std::string::npos);

    auto advanced = eve::executeGameplayControlJson(
        envelope("advance", instanceText, ",\"tick\":2,\"deltaNanoseconds\":16666667"));
    REQUIRE(advanced.ok());
    CHECK(advanced.value().find("\"tick\":2") != std::string::npos);

    auto events = eve::executeGameplayControlJson(
        envelope("events", instanceText, ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("\"events\":[]") != std::string::npos);
}

TEST_CASE("gameplay.control.jsonFacadeRejectsUnknownSchemaFields") {
    auto rejected = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains","mystery":true})");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Rejected);
}

TEST_CASE("gameplay.control.squirrelFacadeUsesTheSharedJsonRouter") {
    eve::dev::DevTool::instance().detach();
    eve::Runtime runtime(128, ssq::Libs::ALL);
    eve::ModuleManager::expose(runtime.vm());
    eve::dev::DevTool::instance().attach(runtime.vm(), false);
    eve::dev::DevTool::instance().exposeScriptApi(runtime.vm());
    runtime.runSource(
        R"SQ(function callGameplay() {
            return eve.dev.gameplay("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"domains\"}");
        })SQ",
        "gameplay-control-facade.nut");
    const std::string response =
        runtime.vm().callFunc(runtime.vm().findFunc("callGameplay"), runtime.vm()).toString();
    CHECK(response.find("\"domains\"") != std::string::npos);
    eve::dev::DevTool::instance().detach();
}
