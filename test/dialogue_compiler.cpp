#include "dialogue/ConversationCompiler.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DnutParser.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueCompiler.singleParserMixedDocumentAndStrictSchema") {
    const std::string source = R"(
schema "eve.dnut"
version 1
pool greeting {
  guide: "A // inside string is not a comment" id="greeting.line"
}
story forest.arrival {
  dialogue id=story.forest.arrival
  wait 0.35
}
conversation greeting.scene entry=end {
  parameter count int optional default=2
  node end end
}
)";
    std::vector<ConversationDiagnostic> diagnostics;
    auto parsed = parseDnutDocument(source, "mixed.dnut", diagnostics);
    REQUIRE(parsed.ok());
    DnutDocument document = std::move(parsed).takeValue();
    CHECK_EQ(document.conversations.size(), size_t(1));
    CHECK_EQ(document.conversations[0].parameters[0].defaultValue.asInt(), 2);
    REQUIRE(document.poolRoot.find("pools") != nullptr);

    diagnostics.clear();
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\nunknown thing\n", "bad.dnut", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnostics[0].code, std::string("DnutParseError"));
}

TEST_CASE("dialogueCompiler.parameterizedExpressionsAndLocalization") {
    const std::string source = R"DNUT(
schema "eve.dnut"
version 1
conversation common.greeting version=3 entry=decide {
parameter speaker string required
parameter listener string required
parameter location string required
node decide branch {
route friendly when="score(speaker, listener, location) >= threshold(speaker.personality)" target=friendly
route formal target=formal
}
node friendly line speaker=speaker pool=greeting.friendly i18n=dialogue.greeting.friendly voice=voice.greeting.friendly next=end
node formal line speaker=speaker text="Good day, {listener.name}." i18n=dialogue.greeting.formal next=end
node unused command kind=operation target="quest.accept" result="accepted" payment(money=2) mutation(subject="player", key="quest.accepted", kind="set", value=true, persistent=true) next=end
node end end
}
)DNUT";
    eve::dnut::StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");
    auto compiled = eve::dnut::compileDnutConversations(source, "greeting.dnut", registry);
    REQUIRE(!compiled.hasErrors());
    auto assets = std::move(compiled.assets);
    CHECK(assets.size() == 1);
    CHECK(assets[0].version == 3);
    CHECK(assets[0].findNode("decide")->routes[0].label == "friendly");
    REQUIRE(assets[0].findNode("unused") != nullptr);
    auto mutations = decodeSequenceStateMutations(assets[0].findNode("unused")->payload);
    REQUIRE(mutations.ok());
    REQUIRE(mutations.value().size() == 1);
    CHECK(mutations.value()[0].persistent);
    auto payment = decodeSequencePayment(assets[0].findNode("unused")->payload);
    REQUIRE(payment.ok());
    CHECK_EQ(payment.value().money, 2);

    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(lintConversations(assets, "greeting.dnut", diagnostics).ok());
    CHECK(diagnostics.size() == 1);
    CHECK(static_cast<int>(diagnostics[0].severity) ==
          static_cast<int>(ConversationDiagnostic::Severity::Warning));
    const std::string csv = exportConversationLocalizationCsv(assets);
    CHECK(csv.find("dialogue.greeting.friendly") != std::string::npos);
    CHECK(csv.find("Good day, {listener.name}.") != std::string::npos);
}

TEST_CASE("dialogueCompiler.domainValidatorsRejectInvalidPaymentPlacementAndValues") {
    eve::dnut::StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");

    auto validChoice = eve::dnut::compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation valid entry=pick {\n"
        "node pick choice {\n"
        "route buy target=end text=\"Buy\" i18n=choice.buy payment(money=1) "
        "mutation(subject=\"player\", key=\"bought\", kind=\"set\", value=true)\n"
        "}\nnode end end\n}\n",
        "valid-choice.dnut", registry);
    REQUIRE(!validChoice.hasErrors());
    const auto& route = validChoice.assets.front().findNode("pick")->routes.front();
    CHECK_EQ(sequenceRoutePayloadString(route, "text"), std::string("Buy"));
    auto routePayment = decodeSequencePayment(route.payload);
    REQUIRE(routePayment.ok());
    CHECK_EQ(routePayment.value().money, 1);
    auto routeMutations = decodeSequenceStateMutations(route.payload);
    REQUIRE(routeMutations.ok());
    CHECK_EQ(routeMutations.value().size(), 1u);

    auto badChoice = eve::dnut::compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation invalid entry=pick {\n"
        "node pick choice {\nroute buy target=end payment(money=0)\n}\n"
        "node end end\n}\n",
        "bad-choice.dnut", registry);
    CHECK(badChoice.hasErrors());

    auto badLine = eve::dnut::compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation invalid entry=line {\n"
        "node line line text=\"hello\" payment(money=1) next=end\n"
        "node end end\n}\n",
        "bad-line.dnut", registry);
    CHECK(badLine.hasErrors());
}
