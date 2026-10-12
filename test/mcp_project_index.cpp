#include <Poco/JSON/Parser.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include "devtools/McpArchitectureTools.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpProjectIndexTools.hpp"
#include "devtools/McpServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
using Object = Poco::JSON::Object;
std::filesystem::path projectRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "examples/rpg-classic";
}
std::string utf8(const std::filesystem::path& path) {
    auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing project fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("Cannot write project fixture");
}
Object::Ptr payload(const std::string& envelope) {
    Poco::JSON::Parser parser;
    auto               outer = parser.parse(envelope).extract<Object::Ptr>();
    return parser.parse(outer->getArray("content")->getObject(0)->getValue<std::string>("text")).extract<Object::Ptr>();
}
Object::Ptr args(const std::filesystem::path& root) {
    Object::Ptr value = new Object();
    value->set("projectDir", utf8(root));
    return value;
}
Object::Ptr refresh(const std::filesystem::path& root) {
    return payload(eve::dev::callMcpProjectIndexTool("eve_project_index_refresh", args(root)));
}
Object::Ptr findNode(Object::Ptr result, const std::string& id) {
    auto nodes = result->getArray("nodes");
    if (nodes)
        for (size_t i = 0; i < nodes->size(); ++i) {
            auto item = nodes->getObject(static_cast<unsigned>(i));
            if (item->getValue<std::string>("id") == id) return item;
        }
    return {};
}
struct Fixture {
    std::filesystem::path root;
    Fixture() {
        root = std::filesystem::temp_directory_path() /
               ("eve-project-index-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot())) {
            if (!entry.is_regular_file() || entry.is_symlink()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".json" && ext != ".nut" && ext != ".dnut" && ext != ".md") continue;
            auto target = root / entry.path().lexically_relative(projectRoot());
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.path(), target);
        }
    }
    ~Fixture() {
        // Delete only the verified, test-owned immediate child of the temp directory.
        std::error_code error;
        if (root.parent_path() == std::filesystem::temp_directory_path() &&
            root.filename().string().starts_with("eve-project-index-"))
            std::filesystem::remove_all(root, error);
    }
};
}  // namespace
TEST_CASE("devtools.projectIndex.realQuestChainAndVersionedContext") {
    auto result = refresh(projectRoot());
    REQUIRE(result->getValue<bool>("ok"));
    CHECK(result->getValue<bool>("declarationsResolved"));
    CHECK_EQ(result->getArray("nodes")->size(), size_t(6));
    auto revision = result->getValue<std::string>("revision");
    CHECK_EQ(revision.size(), size_t(64));
    auto input = args(projectRoot());
    input->set("seed", "quest:quest.slayer");
    input->set("task", "Change quest completion presentation");
    input->set("expectedRevision", revision);
    auto context = payload(eve::dev::callMcpArchitectureTool("eve_task_context", input));
    REQUIRE(context->getValue<bool>("ok"));
    CHECK_EQ(context->getValue<std::string>("schema"), std::string("eve.project-context"));
    CHECK_EQ(context->getValue<int>("schemaVersion"), 1);
    CHECK_EQ(context->getArray("nodes")->size(), size_t(6));
    CHECK_EQ(context->getArray("edges")->size(), size_t(5));
    CHECK_EQ(context->getArray("contracts")->size(), size_t(1));
    CHECK_EQ(context->getArray("tests")->size(), size_t(1));
    CHECK(!context->getValue<bool>("truncated"));
    CHECK(findNode(context, "quest:quest.slayer")->getObject("definition")->has("objectives"));
    CHECK_EQ(findNode(context, "script:onQuestCompleted")->getObject("source")->getValue<std::string>("file"),
             std::string("quest_scene.nut"));
    auto unchanged = refresh(projectRoot());
    CHECK_EQ(unchanged->getValue<std::string>("revision"), revision);
    CHECK_EQ(unchanged->getObject("stats")->getValue<int>("parsed"), 0);
    CHECK(unchanged->getObject("stats")->getValue<int>("reused") > 0);
    input->set("expectedRevision", "old");
    CHECK_EQ(payload(eve::dev::callMcpArchitectureTool("eve_task_context", input))->getValue<std::string>("error"),
             std::string("stale_project_revision"));
    input = args(projectRoot());
    input->set("kind", "json_integer");
    input->set("query", "count");
    auto typed = payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input));
    REQUIRE(typed->getValue<bool>("ok"));
    CHECK(typed->getArray("nodes")->size() > 0);
}
TEST_CASE("devtools.projectIndex.incrementRenameDeleteAndRejectFailedPublication") {
    Fixture fixture;
    auto    first = refresh(fixture.root);
    REQUIRE(first->getValue<bool>("ok"));
    CHECK_EQ(first->getValue<std::string>("revision"), refresh(projectRoot())->getValue<std::string>("revision"));
    const auto original = read(fixture.root / "quest_scene.nut");
    write(fixture.root / "quest_scene.nut", original + "\n// Changed evidence\n");
    auto modified = refresh(fixture.root);
    REQUIRE(modified->getValue<bool>("ok"));
    CHECK(modified->getValue<std::string>("revision") != first->getValue<std::string>("revision"));
    CHECK_EQ(modified->getObject("stats")->getValue<int>("parsed"), 1);
    CHECK_EQ(modified->getObject("stats")->getValue<int>("modified"), 1);
    auto renamedPath = std::filesystem::path(u8"任务.nut");
    std::filesystem::rename(fixture.root / "quest_scene.nut", fixture.root / renamedPath);
    auto renamed = refresh(fixture.root);
    REQUIRE(renamed->getValue<bool>("ok"));
    CHECK_EQ(renamed->getObject("stats")->getArray("renames")->size(), size_t(1));
    CHECK_EQ(renamed->getObject("stats")->getValue<int>("parsed"), 0);
    CHECK_EQ(findNode(renamed, "script:onQuestCompleted")->getObject("source")->getValue<std::string>("file"),
             utf8(renamedPath));
    // Literal dofile remains a broken reference until the project edits its call site.
    CHECK(!renamed->getValue<bool>("declarationsResolved"));
    auto main = read(fixture.root / "main.nut");
    main.replace(main.find("quest_scene.nut"), std::string("quest_scene.nut").size(), utf8(renamedPath));
    write(fixture.root / "main.nut", main);
    auto repaired = refresh(fixture.root);
    REQUIRE(repaired->getValue<bool>("ok"));
    CHECK(repaired->getValue<bool>("declarationsResolved"));
    const auto goodRevision = repaired->getValue<std::string>("revision");
    const auto manifest     = read(fixture.root / "game.semantic.json");
    write(fixture.root / "game.semantic.json", "{broken");
    auto rejected = refresh(fixture.root);
    CHECK(!rejected->getValue<bool>("ok"));
    CHECK_EQ(rejected->getValue<std::string>("lastGoodRevision"), goodRevision);
    write(fixture.root / "game.semantic.json", manifest);
    CHECK_EQ(refresh(fixture.root)->getValue<std::string>("revision"), goodRevision);
    std::filesystem::remove(fixture.root / renamedPath);
    auto deleted = refresh(fixture.root);
    REQUIRE(deleted->getValue<bool>("ok"));
    CHECK_EQ(deleted->getObject("stats")->getValue<int>("removed"), 1);
    CHECK_EQ(findNode(deleted, "script:onQuestCompleted")->getValue<std::string>("status"), std::string("missing"));
    CHECK_EQ(deleted->getArray("edges")->size(), size_t(0));
    auto staleArgs = args(fixture.root);
    staleArgs->set("expectedRevision", goodRevision);
    CHECK_EQ(payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", staleArgs))
                 ->getValue<std::string>("error"),
             std::string("stale_project_revision"));
    write(fixture.root / "data/quests.json", "{broken");
    auto badSource = refresh(fixture.root);
    REQUIRE(badSource->getValue<bool>("ok"));
    auto quest = findNode(badSource, "quest:quest.slayer");
    REQUIRE(quest);
    CHECK_EQ(quest->getValue<std::string>("status"), std::string("missing"));
    CHECK(!quest->has("definition"));
}
TEST_CASE("devtools.projectIndex.budgetsAndUntrustedSource") {
    Fixture fixture;
    write(fixture.root / "untrusted.nut", R"SQ(
        throw "This source must never execute during indexing";
        // function onQuestCompleted(envelope) { return true; }
        local text = "function onQuestCompleted(envelope) {} dofile(\"missing.nut\")";
    )SQ");
    auto result = refresh(fixture.root);
    REQUIRE(result->getValue<bool>("ok"));
    CHECK(result->getValue<bool>("declarationsResolved"));
    auto input = args(fixture.root);
    input->set("seed", "quest:quest.slayer");
    input->set("maxNodes", 1);
    auto bounded = payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input));
    REQUIRE(bounded->getValue<bool>("ok"));
    CHECK(bounded->getValue<bool>("truncated"));
    CHECK_EQ(bounded->getArray("nodes")->size(), size_t(1));
    input->set("maxNodes", 64);
    input->set("maxDepth", 0);
    CHECK(payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<bool>("truncated"));
    input->set("maxDepth", 8);
    input->set("maxBytes", 1024);
    CHECK_EQ(
        payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<std::string>("error"),
        std::string("context_budget_exceeded"));
    input->set("maxNodes", 0);
    CHECK_EQ(
        payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<std::string>("error"),
        std::string("invalid_query"));
    write(fixture.root / "untrusted.nut", "function onQuestCompleted(envelope) { return true; }");
    auto ambiguous = refresh(fixture.root);
    REQUIRE(ambiguous->getValue<bool>("ok"));
    CHECK_EQ(findNode(ambiguous, "script:onQuestCompleted")->getValue<std::string>("status"), std::string("ambiguous"));
    std::filesystem::create_directories(fixture.root / ".hidden");
    write(fixture.root / ".hidden/large.json", std::string(2 * 1024 * 1024 + 1, 'x'));
    CHECK(refresh(fixture.root)->getValue<bool>("ok"));
    write(fixture.root / "large.json", std::string(2 * 1024 * 1024 + 1, 'x'));
    CHECK(!refresh(fixture.root)->getValue<bool>("ok"));
}
TEST_CASE("devtools.projectIndex.stdioDiscoveryAndTaskContext") {
    auto& server = eve::dev::McpServer::instance();
    server.stop();
    auto inputArgs = args(projectRoot());
    inputArgs->set("seed", "quest:quest.slayer");
    inputArgs->set("task", "Inspect RPG completion");
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\"}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/"
        "call\",\"params\":{\"name\":\"eve_task_context\",\"arguments\":" +
        eve::dev::mcpStringify(inputArgs) + "}}\n");
    std::ostringstream output;
    struct Stop {
        eve::dev::McpServer& server;
        ~Stop() { server.stop(); }
    } stop{server};
    REQUIRE(server.listenStdio(input, output));
    for (int i = 0; i < 200; ++i) {
        server.poll();
        if (output.str().find("\"id\":2") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    server.stop();
    CHECK(output.str().find("eve_project_index_refresh") != std::string::npos);
    CHECK(output.str().find("eve_project_index_query") != std::string::npos);
    std::istringstream responses(output.str());
    std::string        line;
    bool               checked = false;
    while (std::getline(responses, line)) {
        Poco::JSON::Parser parser;
        auto               response = parser.parse(line).extract<Object::Ptr>();
        if (response->getValue<int>("id") != 2) continue;
        auto context = payload(eve::dev::mcpStringify(response->getObject("result")));
        REQUIRE(context->getValue<bool>("ok"));
        CHECK_EQ(context->getArray("edges")->size(), size_t(5));
        checked = true;
    }
    CHECK(checked);
}
