#include "dialogue/DialogueFlow.h"
#include "dialogue/Dialogue.h"
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
    CHECK(!flow.loadDnutFileChecked("missing.dnut").ok());
    DialogueMemoryFileSystem filesystem;
    filesystem.content = "schema \"eve.dnut\"\nversion 1\nconversation loaded entry=end {\nnode end end\n}\n";
    eve::cap::provide<eve::service::IFileSystem>(&filesystem);
    REQUIRE(flow.loadDnutFileChecked("memory.dnut").ok());
    CHECK(flow.hasConversation("loaded"));
    eve::cap::revoke<eve::service::IFileSystem>(&filesystem);
}

TEST_CASE("dialogueHotReload.transactionalCrossFileValidation") {
    DialogueFlow      flow;
    const std::string greeting = R"(
schema "eve.dnut"
version 1
conversation greeting entry=line {
node line line text="hello" next=end
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(greeting, "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));

    CHECK(!flow.reloadDnutChecked("not a conversation", "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));
    CHECK(!flow.getLastLoadChanged());

    const std::string brokenReference = R"(
schema "eve.dnut"
version 1
conversation greeting entry=call {
node call call target=missing next=end
node end end
}
)";
    CHECK(!flow.reloadDnutChecked(brokenReference, "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));
    CHECK(flow.getDiagnosticMessage(0).find("missing conversation") != std::string::npos);

    const std::string shared = R"(
schema "eve.dnut"
version 1
conversation shared entry=end {
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(shared, "shared.dnut").ok());
    const std::string validReference = R"(
schema "eve.dnut"
version 1
conversation greeting version=2 entry=call {
node call call target=shared next=end
node end end
}
)";
    CHECK(flow.reloadDnutChecked(validReference, "greeting.dnut").ok());
    CHECK(flow.getLastLoadChanged());
    CHECK(flow.lintAllChecked().ok());
}

TEST_CASE("dialogueHotReload.rejectsDuplicateCrossSourceOwnership") {
    DialogueFlow flow;
    const std::string source = R"(
schema "eve.dnut"
version 1
conversation shared.id entry=end {
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(source, "first.dnut").ok());
    CHECK(!flow.loadDnutChecked(source, "second.dnut").ok());
    CHECK(flow.hasConversation("shared.id"));
    CHECK(flow.removeSourceChecked("first.dnut").ok());
    CHECK(!flow.hasConversation("shared.id"));
}

TEST_CASE("dialogueHotReload.commitsPoolsAndConversationsAsOneWorkspace") {
    DialogueFlow flow;
    Dialogue* dialogue = Dialogue::create();
    const std::string source = R"(
schema "eve.dnut"
version 1
pool greeting { guide: "hello" }
conversation greeting.scene entry=end { node end end }
)";
    REQUIRE(flow.loadDnutChecked(source, "mixed.dnut").ok());
    CHECK(dialogue->hasPool("greeting"));
    CHECK(flow.hasConversation("greeting.scene"));

    const std::string duplicate = R"(
schema "eve.dnut"
version 1
pool greeting { guide: "duplicate" }
conversation other.scene entry=end { node end end }
)";
    CHECK(!flow.loadDnutChecked(duplicate, "other.dnut").ok());
    CHECK(!flow.hasConversation("other.scene"));
    CHECK(dialogue->hasPool("greeting"));
    CHECK(flow.removeSourceChecked("mixed.dnut").ok());
    CHECK(!dialogue->hasPool("greeting"));
}

TEST_CASE("dialogueHotReload.exposesStableRuntimeLintDiagnostics") {
    DialogueFlow flow;
    const std::string source = R"(
schema "eve.dnut"
version 1
conversation diagnostics entry=speak {
node speak line speaker=guide text="Hello" next=invoke
node invoke command kind=operation target="world.open" next=end
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(source, "diagnostics.dnut").ok());
    CHECK(!flow.lintAllChecked().ok());
    bool sawHandler = false;
    bool sawLocalization = false;
    bool sawVoice = false;
    for (int index = 0; index < flow.getDiagnosticCount(); ++index) {
        CHECK(flow.getDiagnosticLine(index) > 0);
        CHECK(flow.getDiagnosticColumn(index) > 0);
        if (flow.getDiagnosticCode(index) == "MissingCommandHandler") {
            sawHandler = true;
            CHECK(flow.getDiagnosticAssetPath(index) == "diagnostics/invoke");
        } else if (flow.getDiagnosticCode(index) == "MissingLocalizationReference") {
            sawLocalization = true;
            CHECK(flow.getDiagnosticAssetPath(index) == "diagnostics/speak");
        } else if (flow.getDiagnosticCode(index) == "MissingVoiceReference") {
            sawVoice = true;
            CHECK(flow.getDiagnosticAssetPath(index) == "diagnostics/speak");
        }
    }
    CHECK(sawHandler);
    CHECK(sawLocalization);
    CHECK(sawVoice);
    flow.setManualCommandMode(true);
    CHECK(flow.lintAllChecked().ok());
}

TEST_CASE("dialogueHotReload.rejectsUnsupportedDnutVersionWithStableDiagnostic") {
    DialogueFlow flow;
    CHECK(!flow.loadDnutChecked("schema \"eve.dnut\"\nversion 999\n", "future.dnut").ok());
    REQUIRE(flow.getDiagnosticCount() == 1);
    CHECK(flow.getDiagnosticCode(0) == "UnsupportedSchemaVersion");
    CHECK(flow.getDiagnosticLine(0) > 0);
    CHECK(flow.getDiagnosticColumn(0) > 0);
}
