#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/ConversationCompiler.h"
#include "dialogue/ConversationImporter.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DnutParser.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// dialogue_authoring.cpp
using namespace eve::dialogue;


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
        applied <- flow.applyDocumentChecked(doc).ok;
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

// dialogue_compiler.cpp
using namespace eve::dialogue;


// dialogue_conversation.cpp
using namespace eve;
using namespace eve::dialogue;
using namespace eve::dnut;

namespace {

[[maybe_unused]] SequenceNode node(std::string id, std::string type, std::string next = {}) {
    SequenceNode value;
    value.id   = std::move(id);
    value.type = std::move(type);
    value.next = std::move(next);
    return value;
}

[[maybe_unused]] SequenceRoute route(std::string label, std::string target, eve::Value condition = {}) {
    SequenceRoute value;
    value.label     = std::move(label);
    value.target    = std::move(target);
    value.condition = std::move(condition);
    return value;
}

[[maybe_unused]] StepKindRegistry dialogueRegistry() {
    StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence steps");
    return registry;
}

[[maybe_unused]] SequenceAsset makeGreeting() {
    SequenceAsset asset;
    asset.id            = "common.greeting";
    asset.entry         = "decide";
    asset.parameters    = {SequenceParameter{"speaker"}, SequenceParameter{"listener"}, SequenceParameter{"location"}};
    SequenceNode decide = node("decide", "branch");
    decide.routes = {route("friendly", "friendly", eve::Value("speaker.mood == happy")), route("formal", "formal")};
    SequenceNode friendly = node("friendly", "line", "end");
    friendly.payload.set("speaker", eve::Value("speaker"));
    friendly.payload.set("pool", eve::Value("greeting.friendly"));
    SequenceNode formal = friendly;
    formal.id           = "formal";
    formal.payload.set("pool", eve::Value("greeting.formal"));
    asset.nodes = {decide, friendly, formal, node("end", "end")};
    return asset;
}

}  // namespace


// dialogue_gameplay_control.cpp
// 对话玩法域契约测试：真实 DialogueFlow 运行器走共享玩法协议，
// 覆盖发现、节点观察、路由选择、权限档位、修订号账本、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


namespace {

constexpr const char* kInstanceId = "00000000-0000-7000-8000-000000000901";
constexpr const char* kOwnerId    = "00000000-0000-7000-8000-000000000902";
constexpr const char* kStrangerId = "00000000-0000-7000-8000-000000000903";

/** 一个带选项的对话：ask(choice) -> shop(line) -> end。 */
constexpr const char* kConversation = R"(
conversation greeting entry=ask
node ask choice speaker=elder
option buy -> shop
option leave -> end
node shop line speaker=elder text="here you go" next=end
node end end
endconversation
)";

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

/** 观察状态里的字符串字段（缺失返回空串）。 */
[[maybe_unused]] std::string stateText(const eve::Value& state, const char* name) {
    const eve::Value* field = member(state, name);
    return field == nullptr || !field->isString() ? std::string{} : field->asString();
}

/** 真实模块 + 已加载的对话，并把运行器发布到共享玩法协议。 */
struct DialogueFixture {
    eve::dialogue::DialogueFlow* flow = nullptr;
    eve::SubjectRef              instance;
    eve::SubjectRef              owner;

    DialogueFixture() {
        flow = eve::dialogue::DialogueFlow::create();
        REQUIRE(flow != nullptr);
        flow->clearGameplayControls();
        const auto loaded = flow->loadDnutChecked(kConversation, "greeting.dnut");
        REQUIRE(loaded.ok());
        REQUIRE_EQ(loaded.value(), 1);
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = flow->publishGameplay(kInstanceId, kOwnerId);
        REQUIRE(published.ok());
    }

    ~DialogueFixture() {
        flow->clearGameplayControls();
        flow->clear();
    }

    DialogueFixture(const DialogueFixture&)            = delete;
    DialogueFixture& operator=(const DialogueFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "dialogue") found = candidate;
        });
        return found;
    }
};

[[maybe_unused]] eve::GameplayCommand command(const DialogueFixture& fixture, const char* id, const char* actionText,
                             std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"dialogue\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                        const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace


TEST_CASE("gameplay.control.dialogueScriptPublishesItsRunner") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local flow = eve.DialogueFlow();
        flow.clearGameplayControls();
        scriptPublished <- flow.publishGameplay(
            "00000000-0000-7000-8000-000000000d01",
            "00000000-0000-7000-8000-000000000d02").ok;
        scriptInstances <- flow.getGameplayControlCount();
        local duplicate = flow.publishGameplay(
            "00000000-0000-7000-8000-000000000d01",
            "00000000-0000-7000-8000-000000000d02");
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = flow.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000d02");
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- flow.unpublishGameplay("00000000-0000-7000-8000-000000000d01").ok;
        scriptRemaining <- flow.getGameplayControlCount();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already published") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}

// dialogue_hot_reload.cpp
using namespace eve::dialogue;

namespace {
class DialogueMemoryFileSystem final : public eve::service::IFileSystem {
public:
    std::string content;
    bool        readFile(const std::string&, std::vector<std::uint8_t>& out) override {
        out.assign(content.begin(), content.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string&) override { return true; }
};
}  // namespace


// dialogue_importers.cpp
using namespace eve::dialogue;
