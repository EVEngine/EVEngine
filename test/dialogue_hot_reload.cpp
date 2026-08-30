#include "dialogue/DialogueFlow.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueHotReload.transactionalCrossFileValidation") {
    DialogueFlow      flow;
    const std::string greeting = R"(
conversation greeting entry=line
node line line text="hello" next=end
node end end
endconversation
)";
    REQUIRE(flow.loadFromDnut(greeting, "greeting.dnut") == 1);
    CHECK(flow.hasConversation("greeting"));

    CHECK(flow.reloadFromDnut("not a conversation", "greeting.dnut") == 0);
    CHECK(flow.hasConversation("greeting"));
    CHECK(!flow.getLastLoadChanged());

    const std::string brokenReference = R"(
conversation greeting entry=call
node call call target=missing next=end
node end end
endconversation
)";
    CHECK(flow.reloadFromDnut(brokenReference, "greeting.dnut") == 0);
    CHECK(flow.hasConversation("greeting"));
    CHECK(flow.getDiagnosticMessage(0).find("missing conversation") != std::string::npos);

    const std::string shared = R"(
conversation shared entry=end
node end end
endconversation
)";
    REQUIRE(flow.loadFromDnut(shared, "shared.dnut") == 1);
    const std::string validReference = R"(
conversation greeting version=2 entry=call
node call call target=shared next=end
node end end
endconversation
)";
    CHECK(flow.reloadFromDnut(validReference, "greeting.dnut") == 1);
    CHECK(flow.getLastLoadChanged());
    CHECK(flow.lintAll());
}
