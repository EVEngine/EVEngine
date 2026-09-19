#include "dialogue/DialogueFlow.h"
#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

namespace {
class DialogueMemoryFileSystem final : public eve::service::IFileSystem {
public:
    std::string content;
    bool readFile(const std::string&, std::vector<std::uint8_t>& out) override {
        out.assign(content.begin(), content.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string&) override { return true; }
};
}  // namespace

TEST_CASE("dialogueHotReload.fileLoadingUsesInjectedCapability") {
    eve::cap::detail::clearAllRaw();
    DialogueFlow flow;
    CHECK_EQ(flow.loadFromDnutFile("missing.dnut"), 0);
    DialogueMemoryFileSystem filesystem;
    filesystem.content = "conversation loaded entry=end\nnode end end\nendconversation\n";
    eve::cap::provide<eve::service::IFileSystem>(&filesystem);
    CHECK_EQ(flow.loadFromDnutFile("memory.dnut"), 1);
    CHECK(flow.hasConversation("loaded"));
    eve::cap::revoke<eve::service::IFileSystem>(&filesystem);
}

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

TEST_CASE("dialogueHotReload.rejectsDuplicateCrossSourceOwnership") {
    DialogueFlow flow;
    const std::string source = R"(
conversation shared.id entry=end
node end end
endconversation
)";
    REQUIRE(flow.loadFromDnut(source, "first.dnut") == 1);
    CHECK(flow.loadFromDnut(source, "second.dnut") == 0);
    CHECK(flow.hasConversation("shared.id"));
    CHECK(flow.removeSource("first.dnut"));
    CHECK(!flow.hasConversation("shared.id"));
}
