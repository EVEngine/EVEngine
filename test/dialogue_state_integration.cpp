#include "dialogue/ConversationAuthoring.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialogueState.h"
#include "statepatch/StateAccessAdapter.h"
#include "statepatch/StatePatch.h"
#include "StatePatchTestSupport.h"
#include "transaction/Transaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using eve::IStateMutation;
using eve::IStateQuery;
using eve::MutationContext;
using eve::MutationReceipt;
using eve::Result;
using eve::StateMutation;
using eve::Value;
using eve::dialogue::CommandRequest;
using eve::dialogue::CommandRequestKind;
using eve::dialogue::CommandResponse;
using eve::dialogue::ConversationAsset;
using eve::dialogue::ConversationRunner;
using eve::dialogue::DialogueFlow;
using eve::dialogue::DialogueStateContext;

class TestWorld final : public IStateQuery {
public:
    std::string subject = "actor";
    Value       mood = Value("calm");
    bool        unlocked = false;

    std::optional<Value> value(std::string_view queriedSubject,
                               std::string_view key) const override {
        if (queriedSubject != subject) return std::nullopt;
        if (key == "mood") return mood;
        return std::nullopt;
    }

    std::optional<bool> hasTag(std::string_view queriedSubject,
                                std::string_view tag) const override {
        if (queriedSubject != subject || tag.empty()) return std::nullopt;
        if (tag == "unlocked") return unlocked;
        return false;
    }

    std::optional<Value> attribute(std::string_view queriedSubject,
                                   std::string_view key) const override {
        if (queriedSubject != subject || key != "reputation") return std::nullopt;
        return Value(12.0);
    }

    std::optional<Value> resource(std::string_view,
                                  std::string_view) const override {
        return std::nullopt;
    }

    std::optional<Value> state(std::string_view queriedSubject,
                               std::string_view key) const override {
        return value(queriedSubject, key);
    }

    std::optional<bool> authority(std::string_view,
                                  std::string_view) const override {
        return std::nullopt;
    }
};

class RecordingMutation final : public IStateMutation {
public:
    int calls = 0;
    std::size_t lastCount = 0;
    MutationContext lastContext;

    Result<MutationReceipt> apply(std::span<const StateMutation> mutations,
                                  const MutationContext& context) override {
        ++calls;
        lastCount = mutations.size();
        lastContext = context;
        return Result<MutationReceipt>::success(
            MutationReceipt{context.transactionId, mutations.size()},
            eve::Status::success(eve::StatusCode::Applied));
    }
};

ConversationAsset makeWorldBranchAsset() {
    ConversationAsset asset;
    asset.id = "p1.world-branch";
    asset.entry = "branch";

    ConversationAsset::Node branch;
    branch.id = "branch";
    branch.kind = ConversationAsset::Node::Kind::Branch;
    branch.routes.emplace_back(
        "world", "world-line",
        Value::Object{{"state", Value("mood")}, {"equals", Value("calm")}});
    branch.routes.emplace_back("else", "local-line");

    ConversationAsset::Node worldLine;
    worldLine.id = "world-line";
    worldLine.kind = ConversationAsset::Node::Kind::Line;
    worldLine.next = "end";

    ConversationAsset::Node localLine = worldLine;
    localLine.id = "local-line";

    ConversationAsset::Node end;
    end.id = "end";
    end.kind = ConversationAsset::Node::Kind::End;

    asset.nodes = {branch, worldLine, localLine, end};
    return asset;
}

ConversationAsset makeFacadeAsset() {
    ConversationAsset asset;
    asset.id = "p1.facade";
    asset.entry = "branch";

    ConversationAsset::Node branch;
    branch.id = "branch";
    branch.kind = ConversationAsset::Node::Kind::Branch;
    branch.routes.emplace_back(
        "custom", "operation", Value::Object{{"policy", Value("facade.test")}});
    branch.routes.emplace_back("else", "end");

    ConversationAsset::Node operation;
    operation.id = "operation";
    operation.kind = ConversationAsset::Node::Kind::Command;
    operation.target = "market.buy";
    operation.expression = "operationResult";
    operation.next = "action";

    ConversationAsset::Node action;
    action.id = "action";
    action.kind = ConversationAsset::Node::Kind::Command;
    action.target = "combat.attack";
    action.commandKind = CommandRequestKind::GameplayAction;
    action.expression = "actionResult";
    action.next = "end";

    ConversationAsset::Node end;
    end.id = "end";
    end.kind = ConversationAsset::Node::Kind::End;

    asset.nodes = {branch, operation, action, end};
    return asset;
}

template <std::size_t N>
std::span<eve::transaction::ITransactionParticipant*> participantSpan(
    std::array<eve::transaction::ITransactionParticipant*, N>& participants) {
    return {participants.data(), participants.size()};
}

}  // namespace

TEST_CASE("dialogueState.conversationLocalsDoNotBecomeWorldState") {
    TestWorld world;
    DialogueStateContext context(world.subject);
    context.setQueryProvider(&world);
    ConversationAsset asset = makeWorldBranchAsset();

    ConversationRunner runner;
    runner.setConditionEvaluator([&context](const Value& specification) {
        return context.evaluate(specification);
    });

    eve::StateValue bindings = eve::StateValue::object();
    bindings.set("mood", eve::StateValue::string("local-mood"));
    std::string error;
    CHECK(runner.start(&asset, std::move(bindings), &error));
    CHECK(error.empty());
    CHECK(runner.currentNodeId() == "world-line");
    CHECK(runner.bindings().find("mood")->asString() == "local-mood");

    runner.locals().set("localOnly", eve::StateValue::boolean(true));
    CHECK(!world.value(world.subject, "localOnly").has_value());
    REQUIRE(runner.lastConditionResult() != nullptr);
    CHECK(runner.lastConditionResult()->passed());
    CHECK(static_cast<int>(runner.lastConditionResult()->reasonCode()) ==
          static_cast<int>(eve::decision::ConditionReasonCode::Passed));
}

TEST_CASE("dialogueState.usesSharedConditionExplanation") {
    TestWorld world;
    world.unlocked = false;
    DialogueStateContext context(world.subject);
    context.setQueryProvider(&world);
    const Value specification = Value::Object{{"tag", Value("unlocked")}};

    const auto result = context.evaluate(specification);
    CHECK(!result.passed());
    CHECK(static_cast<int>(result.reasonCode()) ==
          static_cast<int>(eve::decision::ConditionReasonCode::TagMissing));
    CHECK(std::string(eve::decision::conditionReasonCodeName(result.reasonCode())) == "tag_missing");
    const Value* tag = result.details().find("tag");
    REQUIRE(tag != nullptr);
    CHECK(tag->asString() == "unlocked");

    const Value comparison = Value::Object{{"attribute", Value("reputation")},
                                           {"op", Value("gte")},
                                           {"value", Value(10.0)}};
    const auto comparisonResult = context.evaluate(comparison);
    CHECK(comparisonResult.passed());
    CHECK(static_cast<int>(comparisonResult.reasonCode()) ==
          static_cast<int>(eve::decision::ConditionReasonCode::Passed));
}

TEST_CASE("dialogueState.statePatchTransactionFailureDoesNotPartiallyModify") {
    eve::statepatch::Store store;
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    CHECK(seed.view->set("actor", "value", "1"));
    REQUIRE(store.commit(seed.view.get()));
    const std::string before = store.snapshotJson();
    const auto beforeRevision = store.revision();
    const auto beforeEvents = store.eventCount();

    eve::statepatch::StatePatchStateAdapter adapter(store);
    std::array<StateMutation, 2> mutations{
        StateMutation{"actor", "value", Value::integer(2), eve::MutationKind::Set, true},
        StateMutation{"actor", "other", Value("not-a-number"), eve::MutationKind::AddNumber, true},
    };
    const auto rejected = adapter.apply(mutations, MutationContext{"tx-dialogue", "flow", "choice"});
    CHECK(!rejected.ok());
    CHECK(static_cast<int>(rejected.code()) == static_cast<int>(eve::StatusCode::Rejected));
    CHECK(store.snapshotJson() == before);
    CHECK(store.revision() == beforeRevision);
    CHECK(store.eventCount() == beforeEvents);

    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(batch.view->set("actor", "value", "2"));
    CHECK(!batch.view->set("actor", "broken", "{"));
    eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
    std::array<eve::transaction::ITransactionParticipant*, 1> participants{&participant};
    eve::transaction::Coordinator coordinator;
    const auto transactionResult = coordinator.execute(
        eve::transaction::TransactionContext("tx-dialogue-invalid"), participantSpan(participants));
    CHECK(!transactionResult.ok());
    CHECK(store.snapshotJson() == before);
    CHECK(store.revision() == beforeRevision);
    CHECK(store.eventCount() == beforeEvents);
}

TEST_CASE("dialogueState.valueRoundtripAndLegacyFacadeParity") {
    static_assert(std::is_same_v<eve::dialogue::Dialogue::DataValue, eve::Value>);
    static_assert(std::is_same_v<eve::dialogue::Dialogue::VarValue, eve::Value>);

    eve::StateValue legacy = eve::StateValue::object();
    eve::StateValue values = eve::StateValue::array();
    values.pushBack(eve::StateValue::null());
    values.pushBack(eve::StateValue::integer(-7));
    values.pushBack(eve::StateValue::number(2.5));
    values.pushBack(eve::StateValue::boolean(true));
    values.pushBack(eve::StateValue::string("中文🚀"));
    legacy.set("values", std::move(values));
    legacy.set("name", eve::StateValue::string("dialogue"));

    const Value canonical = eve::dialogue::toCanonicalValue(legacy);
    const eve::StateValue restored = eve::dialogue::toDialogueStateValue(canonical);
    CHECK(restored == legacy);

    auto encoded = canonical.toJson();
    REQUIRE(encoded.ok());
    auto parsed = Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == canonical);

    eve::dialogue::Dialogue::DataValue oldData =
        eve::dialogue::Dialogue::DataValue::object(eve::Value::Object{});
    oldData.set("answer", eve::dialogue::Dialogue::VarValue::integer(42));
    auto oldEncoded = oldData.toJson();
    REQUIRE(oldEncoded.ok());
    CHECK(oldEncoded.value() == R"({"answer":42})");
    CHECK(oldData.find("answer")->asInt() == 42);
}

TEST_CASE("dialogueState.dialogueFlowConfiguresAllCrossDomainHooks") {
    TestWorld world;
    RecordingMutation mutation;
    DialogueFlow flow;
    bool customConditionCalled = false;
    std::vector<CommandRequest> requests;

    DialogueFlow::IntegrationConfig config;
    config.subject = world.subject;
    config.stateQuery = &world;
    config.stateMutation = &mutation;
    config.conditionEvaluator = [&customConditionCalled](const Value&) {
        customConditionCalled = true;
        return eve::decision::ConditionResult::success(Value(true));
    };
    config.operationHandler = [&requests](const CommandRequest& request) {
        requests.push_back(request);
        CommandResponse response;
        response.value = Value::integer(7);
        return response;
    };
    config.gameplayActionHandler = [&requests](const CommandRequest& request) {
        requests.push_back(request);
        CommandResponse response;
        response.value = Value::string("done");
        return response;
    };
    flow.configureIntegration(std::move(config));

    const auto worldMood = flow.stateContext().value("mood");
    REQUIRE(worldMood.has_value());
    CHECK(worldMood->asString() == "calm");

    std::array<StateMutation, 1> stateMutation{
        StateMutation{"actor", "flag", Value(true), eve::MutationKind::Set, false}};
    const auto mutationResult = flow.applyStateMutations(
        stateMutation, MutationContext{"tx-facade", "flow", "dialogue"});
    REQUIRE(mutationResult.ok());
    CHECK(mutation.calls == 1);
    CHECK(mutation.lastCount == 1);
    CHECK(mutation.lastContext.transactionId == "tx-facade");

    ConversationAsset asset = makeFacadeAsset();
    auto* document = new eve::dialogue::ConversationDocument(asset);
    CHECK(flow.applyDocument(document));
    delete document;

    std::string error;
    CHECK(flow.start(asset.id, ssq::Object{}));
    CHECK(error.empty());
    CHECK(customConditionCalled);
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].name == "market.buy");
    CHECK(static_cast<int>(requests[0].kind) == static_cast<int>(CommandRequestKind::Operation));
    CHECK(requests[1].name == "combat.attack");
    CHECK(static_cast<int>(requests[1].kind) == static_cast<int>(CommandRequestKind::GameplayAction));
    CHECK(!flow.isActive());
}
