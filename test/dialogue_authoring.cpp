#include "common/Module.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/DialogueFlow.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::dialogue;

TEST_CASE("dialogueAuthoring.documentGraphEditingAndDiagnostics") {
    ConversationDocument document("quest.intro");
    REQUIRE(document.addParameter("player"));
    REQUIRE(document.addNode("start", "line"));
    REQUIRE(document.setEntry("start"));
    REQUIRE(document.setField("start", "speaker", "guide"));
    REQUIRE(document.setField("start", "text", "Welcome, {player.name}."));
    REQUIRE(document.setField("start", "next", "decision"));
    REQUIRE(document.addNode("decision", "choice"));
    REQUIRE(document.addRoute("decision", "accept", "end"));

    CHECK_EQ(document.getNodeCount(), 3);
    CHECK_EQ(document.getFieldKind("start", 2), std::string("multiline"));
    CHECK_EQ(document.getRouteLabel("decision", 0), std::string("accept"));
    CHECK(document.validate());

    REQUIRE(document.renameNode("decision", "offer"));
    CHECK_EQ(document.getField("start", "next"), std::string("offer"));
    REQUIRE(document.removeNode("end"));
    CHECK(!document.validate());
    CHECK(document.getDiagnosticCount() > 0);
    CHECK_EQ(document.getDiagnosticSeverity(0), std::string("error"));
    CHECK(!document.getDiagnosticMessage(0).empty());
}

TEST_CASE("dialogueAuthoring.scriptComposesInspectorAndAppliesDocument") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        flow <- eve.DialogueFlow();
        doc <- flow.newDocument("quest.editor");
        added <- doc.addNode("line", "line");
        entrySet <- doc.setEntry("line");
        textSet <- doc.setField("line", "text", "Composable editor");
        nextSet <- doc.setField("line", "next", "end");
        fieldCount <- doc.getFieldCount("line");
        fieldNames <- [];
        fieldKinds <- [];
        for (local i = 0; i < fieldCount; ++i) {
            fieldNames.append(doc.getFieldName("line", i));
            fieldKinds.append(doc.getFieldKind("line", i));
        }
        valid <- doc.validate();
        applied <- flow.applyDocument(doc);
        copy <- flow.getDocument("quest.editor");
        copiedText <- copy.getField("line", "text");
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("entrySet").toBool());
    CHECK(vm.find("textSet").toBool());
    CHECK(vm.find("nextSet").toBool());
    CHECK(vm.find("fieldCount").toInt() >= 3);
    CHECK(vm.find("valid").toBool());
    CHECK(vm.find("applied").toBool());
    CHECK_EQ(vm.find("copiedText").toString(), std::string("Composable editor"));
}
