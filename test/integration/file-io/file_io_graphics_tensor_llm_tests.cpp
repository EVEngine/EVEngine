#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_language_server)
#include "PrivateTestScope.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include "devtools/LanguageServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// language_server.cpp
namespace test_language_server {
using eve::dev::LanguageServer;
using eve::dev::LspDispatch;
using eve::dev::lsp::Position;

namespace {

std::filesystem::path tempProject(const char *tag) {
    const auto      path = std::filesystem::temp_directory_path() / tag;
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path / "scripts", error);
    return path;
}

void writeFile(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

std::string readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool hasLabel(const std::vector<eve::script::ScriptCompletion> &items, std::string_view label) {
    for (const auto &item : items)
        if (item.label == label) return true;
    return false;
}

}  // namespace

TEST_CASE("devtools.languageServer.diagnosticsCompletionHoverAndSymbols") {
    const auto root = tempProject("eve_ut_language_server_api");
    writeFile(root / "config.nut", "config = { width = 640 height = 360 title = \"lsp\" modules = [\"gfx\"] }\n");
    writeFile(root / "scripts" / "lib.nut", "export function greet(name: string) -> string { return name }\n");
    writeFile(root / "main.nut",
              "import { greet } from \"./scripts/lib.nut\"\n"
              "local playerSpeed: float = 2.0\n"
              "function boom() { return await 1 }\n"
              "gfx.setBackgroundColor(0.1, 0.2, 0.3, 1.0)\n"
              "local message = greet(\"Ada\")\n");

    LanguageServer    server(root.string());
    const std::string mainUri = LanguageServer::fileUriFromPath((root / "main.nut").string());
    const std::string source  = readFile(root / "main.nut");
    server.openDocument(mainUri, source);

    const auto diagnostics = server.diagnosticsFor(mainUri);
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnostics.front().code, std::string("EVE2601"));

    const auto keywords = server.complete(mainUri, Position{1, 0});
    CHECK(hasLabel(keywords, "local"));
    CHECK(hasLabel(keywords, "playerSpeed"));

    const auto hover = server.hover(mainUri, Position{1, 8});
    REQUIRE(hover.has_value());
    CHECK(hover->markdown.find("playerSpeed") != std::string::npos);

    const auto symbols = server.documentSymbols(mainUri);
    REQUIRE(!symbols.empty());
    bool foundSpeed = false;
    for (const auto &symbol : symbols)
        if (symbol.name == "playerSpeed") foundSpeed = true;
    CHECK(foundSpeed);

    const auto definition = server.definition(mainUri, Position{4, 17});
    REQUIRE(definition.has_value());
    CHECK(definition->uri.find("lib.nut") != std::string::npos);

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("devtools.languageServer.memberCompletionAndSignatureHelp") {
    const auto root = tempProject("eve_ut_language_server_members");
    writeFile(root / "config.nut", "config = { width = 320 height = 180 title = \"lsp\" }\n");
    writeFile(root / "main.nut", "gfx.setBackgroundColor(0.1, 0.2, 0.3, 1.0)\n");

    LanguageServer    server(root.string());
    const std::string uri = LanguageServer::fileUriFromPath((root / "main.nut").string());
    server.openDocument(uri, "gfx.setBackgroundColor(0.1, 0.2, 0.3, 1.0)\n");

    const auto members = server.complete(uri, Position{0, 4});
    CHECK(hasLabel(members, "setBackgroundColor"));

    const auto help = server.signatureHelp(uri, Position{0, 24});
    REQUIRE(help.has_value());
    CHECK(help->label.find("setBackgroundColor") != std::string::npos);
    CHECK(help->activeParameter >= 0);

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("devtools.languageServer.completesChainedAndConstructedNativeTypes") {
    const auto root = tempProject("eve_ut_language_server_chain");
    writeFile(root / "config.nut", "config = { width = 320 height = 180 title = \"lsp\" }\n");
    writeFile(root / "main.nut",
              "physics.newWorld(0, 900).\n"
              "eve.Physics().\n"
              "local world = physics.newWorld(0, 900, true)\n"
              "world.\n"
              "particles.newEmitter(256).\n");

    LanguageServer    server(root.string());
    const std::string uri    = LanguageServer::fileUriFromPath((root / "main.nut").string());
    const std::string source = readFile(root / "main.nut");
    server.openDocument(uri, source);

    const auto chained = server.complete(uri, Position{0, 27});
    CHECK(hasLabel(chained, "setGravity"));

    const auto constructed = server.complete(uri, Position{1, 15});
    CHECK(hasLabel(constructed, "newWorld"));

    const auto fromLocal = server.complete(uri, Position{3, 6});
    CHECK(hasLabel(fromLocal, "setGravity"));

    const auto emitter = server.complete(uri, Position{4, 27});
    CHECK(hasLabel(emitter, "applyPreset"));

    const auto eveClasses = server.complete(uri, Position{1, 4});
    CHECK(hasLabel(eveClasses, "Physics"));

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("devtools.languageServer.jsonRpcInitializeAndDiagnostics") {
    const auto root = tempProject("eve_ut_language_server_jsonrpc");
    writeFile(root / "config.nut", "config = { width = 320 height = 180 title = \"lsp\" }\n");
    writeFile(root / "main.nut", "function boom() { return await 1 }\n");

    LanguageServer     server(root.string());
    const std::string  uri = LanguageServer::fileUriFromPath((root / "main.nut").string());
    std::ostringstream output;
    CHECK(server.handleMessage(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", output) ==
          LspDispatch::Continue);
    CHECK(output.str().find("documentSymbolProvider") != std::string::npos);
    CHECK(output.str().find("signatureHelpProvider") != std::string::npos);
    CHECK(output.str().find("\"change\":2") != std::string::npos);
    CHECK(output.str().find("documentFormattingProvider") != std::string::npos);
    CHECK(output.str().find("foldingRangeProvider") != std::string::npos);
    CHECK(output.str().find("semanticTokensProvider") != std::string::npos);
    const bool incrementalSync = output.str().find("\"change\":2") != std::string::npos ||
                                 output.str().find("\"change\": 2") != std::string::npos;
    CHECK(incrementalSync);

    output.str("");
    output.clear();
    const std::string open =
        std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") + uri +
        R"(","languageId":"nut","version":1,"text":"function boom() { return await 1 }\n"}}})";
    CHECK(server.handleMessage(open, output) == LspDispatch::Continue);
    CHECK(output.str().find("publishDiagnostics") != std::string::npos);
    CHECK(output.str().find("EVE2601") != std::string::npos);

    output.str("");
    output.clear();
    CHECK(server.handleMessage(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})", output) == LspDispatch::Continue);
    CHECK(server.handleMessage(R"({"jsonrpc":"2.0","method":"exit"})", output) == LspDispatch::Exit);

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("devtools.languageServer.formatFoldAndIncrementalEdit") {
    const auto root = tempProject("eve_ut_language_server_format");
    writeFile(root / "config.nut", "config = { width = 320 height = 180 title = \"lsp\" }\n");
    writeFile(root / "main.nut", "function foo() {\nlocal x = 1\n}\n");

    LanguageServer    server(root.string());
    const std::string uri = LanguageServer::fileUriFromPath((root / "main.nut").string());
    server.openDocument(uri, "function foo() {\nlocal x = 1\n}\n");

    const auto edits = server.formatDocument(uri);
    REQUIRE(edits.size() == 1);
    CHECK_EQ(edits.front().newText, std::string("function foo() {\n    local x = 1\n}\n"));

    eve::dev::lsp::Range range{{1, 0}, {2, 0}};
    const auto           ranged = server.formatDocument(uri, {}, &range);
    REQUIRE(ranged.size() == 1);
    CHECK_EQ(ranged.front().newText, std::string("    local x = 1\n"));

    const auto folds = server.foldingRanges(uri);
    REQUIRE(!folds.empty());
    CHECK_EQ(folds.front().kind, std::string("region"));
    CHECK_EQ(folds.front().startLine, size_t(0));
    CHECK_EQ(folds.front().endLine, size_t(2));

    server.changeDocument(uri, eve::dev::lsp::Range{{1, 6}, {1, 7}}, "value");
    const auto hover = server.hover(uri, Position{1, 8});
    REQUIRE(hover.has_value());
    CHECK(hover->markdown.find("value") != std::string::npos);

    std::ostringstream output;
    const std::string  change =
        std::string(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")") + uri +
        R"(","version":2},"contentChanges":[{"range":{"start":{"line":1,"character":6},"end":{"line":1,"character":11}},"text":"n"}]}})";
    CHECK(server.handleMessage(change, output) == LspDispatch::Continue);
    const auto afterIncremental = server.hover(uri, Position{1, 6});
    REQUIRE(afterIncremental.has_value());
    CHECK(afterIncremental->markdown.find("n") != std::string::npos);

    output.str("");
    output.clear();
    CHECK(server.handleMessage(
              std::string(
                  R"({"jsonrpc":"2.0","id":3,"method":"textDocument/formatting","params":{"textDocument":{"uri":")") +
                  uri + R"(},"options":{"tabSize":4,"insertSpaces":true}}})",
              output) == LspDispatch::Continue);
    CHECK(output.str().find("local n = 1") != std::string::npos);

    output.str("");
    output.clear();
    CHECK(server.handleMessage(
              std::string(
                  R"({"jsonrpc":"2.0","id":4,"method":"textDocument/foldingRange","params":{"textDocument":{"uri":")") +
                  uri + R"("}}})",
              output) == LspDispatch::Continue);
    CHECK(output.str().find("startLine") != std::string::npos);

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

TEST_CASE("devtools.languageServer.semanticTokensClassFunctionAndSlot") {
    const auto root = tempProject("eve_ut_language_server_tokens");
    writeFile(root / "config.nut", "config = { width = 320 height = 180 title = \"lsp\" }\n");
    writeFile(root / "main.nut", "class Hero {}\ngfx.setBackgroundColor(0.1, 0.2, 0.3, 1.0)\nlocal hero = Hero()\n");

    LanguageServer    server(root.string());
    const std::string uri = LanguageServer::fileUriFromPath((root / "main.nut").string());
    server.openDocument(uri, "class Hero {}\ngfx.setBackgroundColor(0.1, 0.2, 0.3, 1.0)\nlocal hero = Hero()\n");

    const auto tokens = server.semanticTokens(uri);
    const auto typeAt = [&](std::string_view name, size_t line) -> uint32_t {
        for (const auto &token : tokens)
            if (token.name == name && token.start.line == line) return token.type;
        return 99;
    };
    CHECK_EQ(typeAt("Hero", 0), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("gfx", 1), eve::dev::lsp::SemanticTypes::Namespace);
    CHECK_EQ(typeAt("setBackgroundColor", 1), eve::dev::lsp::SemanticTypes::Method);
    CHECK_EQ(typeAt("hero", 2), eve::dev::lsp::SemanticTypes::Variable);
    CHECK_EQ(typeAt("Hero", 2), eve::dev::lsp::SemanticTypes::Class);

    std::error_code error;
    std::filesystem::remove_all(root, error);
}
}  // namespace test_language_server
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_dual_grid_masks)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include "filesystem/FileData.h"
#include "image/ImageData.h"
#include "map/DualGrid.h"
// map_dual_grid_masks.cpp
namespace test_map_dual_grid_masks {
using namespace eve::map;

namespace {

[[maybe_unused]] DualGridRgbaImage loadAtlasTile(const char *path, int tileX, int tileY) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::vector<uint8_t>      bytes{std::istreambuf_iterator<char>(input), {}};
    eve::filesystem::FileData encoded(path, bytes.size());
    std::memcpy(encoded.getData(), bytes.data(), bytes.size());
    eve::image::ImageData atlas(&encoded);
    REQUIRE_EQ(atlas.getFormat(), std::string("RGBA8"));
    REQUIRE(atlas.getWidth() >= (tileX + 1) * 256);
    REQUIRE(atlas.getHeight() >= (tileY + 1) * 128);

    DualGridRgbaImage tile{256, 128, std::vector<uint8_t>(256u * 128u * 4u)};
    const auto       *source = static_cast<const uint8_t *>(atlas.getData());
    for (int y = 0; y < tile.height; ++y)
        for (int x = 0; x < tile.width; ++x) {
            const size_t src = size_t(((tileY * 128 + y) * atlas.getWidth() + tileX * 256 + x) * 4);
            const size_t dst = size_t((y * tile.width + x) * 4);
            std::memcpy(tile.pixels.data() + dst, source + src, 4);
            const float diamond = std::abs((float(x) + 0.5f) / 128.f - 1.f) + std::abs((float(y) + 0.5f) / 64.f - 1.f);
            if (diamond > 1.f) {
                tile.pixels[dst] = tile.pixels[dst + 1] = tile.pixels[dst + 2] = 0;
                tile.pixels[dst + 3]                                           = 0;
            }
        }
    return tile;
}

}  // namespace


TEST_CASE("map.dualGridMask.packagedSbsFixturesDecode") {
    const auto grass = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/terrain-1.png", 1, 0);
    const auto sand  = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/terrain-1.png", 0, 5);
    const auto stone = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/terrain-2.png", 0, 0);
    const auto water = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/water.png", 0, 0);
    CHECK_EQ(grass.width, 256);
    CHECK_EQ(sand.height, 128);
    CHECK_EQ(stone.pixels.size(), size_t(256 * 128 * 4));
    CHECK_EQ(water.pixels.size(), size_t(256 * 128 * 4));
}

}  // namespace test_map_dual_grid_masks
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_project_index)
#include "PrivateTestScope.h"
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include "PathBesideSource.h"
#include "devtools/McpArchitectureTools.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpProjectIndexTools.hpp"
#include "devtools/McpServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_project_index.cpp
namespace test_mcp_project_index {
namespace {
using Object = Poco::JSON::Object;
std::filesystem::path projectRoot() {
    return std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path() /
           "examples/rpg-classic";
}
std::string utf8(const std::filesystem::path &path) {
    auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing project fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("Cannot write project fixture");
}
Object::Ptr payload(const std::string &envelope) {
    Poco::JSON::Parser parser;
    auto               outer = parser.parse(envelope).extract<Object::Ptr>();
    return parser.parse(outer->getArray("content")->getObject(0)->getValue<std::string>("text")).extract<Object::Ptr>();
}
Object::Ptr args(const std::filesystem::path &root) {
    Object::Ptr value = new Object();
    value->set("projectDir", utf8(root));
    return value;
}
Object::Ptr refresh(const std::filesystem::path &root) {
    return payload(eve::dev::callMcpProjectIndexTool("eve_project_index_refresh", args(root)));
}
Object::Ptr findNode(Object::Ptr result, const std::string &id) {
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
    Fixture(bool legacy = true, bool copyProject = true) {
        root = std::filesystem::temp_directory_path() /
               ("eve-project-index-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        if (copyProject)
            for (const auto &entry : std::filesystem::recursive_directory_iterator(projectRoot())) {
                if (!entry.is_regular_file() || entry.is_symlink()) continue;
                auto ext = entry.path().extension().string();
                if (ext != ".json" && ext != ".nut" && ext != ".dnut" && ext != ".md") continue;
                auto target = root / entry.path().lexically_relative(projectRoot());
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(entry.path(), target);
            }
        if (legacy)
            write(root / "game.semantic.json",
                  read(std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() /
                       "fixtures/project_semantic_v1.json"));
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
    CHECK(result->getArray("nodes")->size() >= 6);
    auto revision = result->getValue<std::string>("revision");
    CHECK_EQ(revision.size(), size_t(64));
    auto input = args(projectRoot());
    input->set("seed", "quest:quest.slayer");
    input->set("task", "Change quest completion presentation");
    input->set("expectedRevision", revision);
    auto context = payload(eve::dev::callMcpArchitectureTool("eve_task_context", input));
    REQUIRE(context->getValue<bool>("ok"));
    CHECK_EQ(context->getValue<std::string>("schema"), std::string("eve.project-context"));
    CHECK_EQ(context->getValue<int>("schemaVersion"), 2);
    REQUIRE(findNode(context, "event:quest.completed"));
    REQUIRE(findNode(context, "entity:rpg-classic.quests/quest.slayer.marker"));
    REQUIRE(findNode(context, "api:scene/SceneNodeRef.setVisible"));
    REQUIRE(findNode(context, "test:runQuestSceneProbe"));
    CHECK(context->getArray("contracts")->size() >= 1);
    CHECK(context->getArray("tests")->size() >= 1);
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
    auto &server = eve::dev::McpServer::instance();
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
        eve::dev::McpServer &server;
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
        CHECK(findNode(context, "entity:rpg-classic.quests/quest.slayer.marker"));
        checked = true;
    }
    CHECK(checked);
}
TEST_CASE("devtools.projectIndex.automaticWithoutManifestReflectsDomainChainAndFields") {
    Fixture fixture(false, false);
    write(
        fixture.root / "quests.json",
        R"JSON([{"id":"quest.new","objectives":[{"id":"kill","topic":"kill","target":"slime","count":1}],"completePolicy":"claim"}])JSON");
    const std::string script = R"SQ(
        throw "Indexing must not execute this script";
        class Actor { health: int = 100; label: string = "hero"; }
        marker <- null;
        log <- eve.GameEvent().newLog();
        function initialize() {
            local scene = eve.Scene();
            marker = scene.getNodeRefAt("demo", "node");
        }
        function afterEvent(envelope) {
            if (envelope.getType() != "event.finished" || envelope.getSubject() != "quest.new") return false;
            return marker.setVisible(true);
        }
        function process() {
            local result = log.append("00000000-0000-4000-8000-000000000001", "event.finished", "demo", "quest.new", "", "", 0, 0, "{}");
            return afterEvent(log.at(0));
        }
    )SQ";
    write(fixture.root / "world.nut", script);
    std::filesystem::create_directories(fixture.root / "tests");
    write(fixture.root / "tests/check.nut", "function runProbe() { return process(); }");
    auto first = refresh(fixture.root);
    REQUIRE(first->getValue<bool>("ok"));
    CHECK_EQ(first->getValue<int>("schemaVersion"), 2);
    auto input = args(fixture.root);
    input->set("seed", "quest:quest.new");
    input->set("task", "Inspect inferred chain");
    auto context = payload(eve::dev::callMcpArchitectureTool("eve_task_context", input));
    REQUIRE(context->getValue<bool>("ok"));
    CHECK(findNode(context, "event:event.finished"));
    CHECK(findNode(context, "script:afterEvent"));
    CHECK(findNode(context, "entity:demo/node"));
    CHECK(findNode(context, "api:scene/SceneNodeRef.setVisible"));
    CHECK(findNode(context, "test:runProbe"));
    input = args(fixture.root);
    input->set("kind", "script_class");
    auto classes = payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input));
    auto actor   = findNode(classes, "class:Actor");
    REQUIRE(actor);
    CHECK_EQ(actor->getArray("fields")->getObject(0)->getValue<std::string>("type"), std::string("int"));
    CHECK_EQ(refresh(fixture.root)->getObject("stats")->getValue<int>("parsed"), 0);
    std::filesystem::rename(fixture.root / "world.nut", fixture.root / "renamed.nut");
    auto renamed = refresh(fixture.root);
    CHECK_EQ(renamed->getObject("stats")->getArray("renames")->size(), size_t(1));
    CHECK_EQ(renamed->getObject("stats")->getValue<int>("parsed"), 0);
    input = args(fixture.root);
    input->set("seed", "script:afterEvent");
    auto relocated = payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input));
    CHECK_EQ(findNode(relocated, "script:afterEvent")->getObject("source")->getValue<std::string>("file"),
             std::string("renamed.nut"));
    auto changed = script;
    changed.replace(changed.find("\"node\""), 6, "\"new-node\"");
    write(fixture.root / "renamed.nut", changed);
    input->set("expectedRevision", first->getValue<std::string>("revision"));
    CHECK_EQ(
        payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<std::string>("error"),
        std::string("stale_project_revision"));
    input = args(fixture.root);
    input->set("seed", "entity:demo/node");
    CHECK_EQ(
        payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<std::string>("error"),
        std::string("unknown_seed"));
    std::filesystem::remove(fixture.root / "renamed.nut");
    input->set("seed", "script:afterEvent");
    CHECK_EQ(
        payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<std::string>("error"),
        std::string("unknown_seed"));
}
TEST_CASE("devtools.projectIndex.automaticRejectsShadowedAndDynamicNativeLookalikes") {
    Fixture fixture(false, false);
    write(fixture.root / "world.nut", R"SQ(
        marker <- null;
        function initialize() { local scene = eve.Scene(); marker = scene.getNodeRefAt("demo", "node"); }
        function writeMarker() { return marker.setVisible(true); }
        function shadow() { local marker = unknown(); return marker.setVisible(true); }
        function fake() { local eve = lookalike; local scene = eve.Scene(); return scene.getNodeRefAt("wrong", "fake"); }
    )SQ");
    auto input = args(fixture.root);
    input->set("seed", "script:shadow");
    auto shadow = payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input));
    REQUIRE(shadow->getValue<bool>("ok"));
    CHECK(!findNode(shadow, "api:scene/SceneNodeRef.setVisible"));
    CHECK(findNode(shadow, "script:shadow")->getArray("unknownCalls")->size() > 0);
    input->set("seed", "entity:wrong/fake");
    CHECK_EQ(
        payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input))->getValue<std::string>("error"),
        std::string("unknown_seed"));
    auto script = read(fixture.root / "world.nut");
    write(fixture.root / "world.nut", script + "\nfunction retarget() { marker = unresolved(); }\n");
    input->set("seed", "script:writeMarker");
    auto dynamic = payload(eve::dev::callMcpProjectIndexTool("eve_project_index_query", input));
    REQUIRE(dynamic->getValue<bool>("ok"));
    CHECK(!findNode(dynamic, "api:scene/SceneNodeRef.setVisible"));
    CHECK(findNode(dynamic, "script:writeMarker")->getArray("unknownCalls")->size() > 0);
}
}  // namespace test_mcp_project_index
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_photo_mode_session)
#include "PrivateTestScope.h"
#include <fstream>
#include <limits>
#include "ui/PcgPhotoModeSession.h"
#include "ui/PcgPhotoModeValues.h"
#include "zeroerr/unittest.h"
// pcg_photo_mode_session.cpp
namespace test_pcg_photo_mode_session {
// pcg_photo_mode_session.cpp
using namespace eve::ui;
namespace {
[[maybe_unused]] std::string profile(const char *scene, int64_t light, float fov) {
    PcgPhotoModeValues v;
    v.setString("m_lastSceneName", scene).value();
    v.setBool("m_isUsingPcgLighting", light >= 0).value();
    v.setInt("m_selectedPcgLightingProfile", light).value();
    v.setFloat("m_fieldOfView", fov).value();
    auto j = v.snapshotJson();
    j.value();
    return std::move(j).takeValue();
}
[[maybe_unused]] float fov(const std::string &j) {
    PcgPhotoModeValues v;
    v.restoreJson(j).value();
    auto x = v.getFloat("m_fieldOfView");
    x.value();
    return std::move(x).takeValue();
}
}  // namespace


// pcg_photo_mode_values.cpp
using namespace eve::ui;


TEST_CASE("ui.pcgPhotoModeValues.roundTripsAllFieldsAndRejectsUnknownSchema") {
    PcgPhotoModeValues source;
    REQUIRE(source.setBool("m_showFPS", true).ok());
    REQUIRE(source.setInt("m_targetFPS", 144).ok());
    REQUIRE(source.setFloat("m_pcgWindSpeed", 0.875f).ok());
    REQUIRE(source.setString("m_lastSceneName", "Alpine World").ok());
    REQUIRE(source.setColor("m_sunColor", 1.25f, .5f, .25f, 2.0f).ok());
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "1\\n";
    auto encoded = source.snapshotJson();
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "2\\n";
    REQUIRE(encoded.ok());
    PcgPhotoModeValues restored;
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "3\\n";
    REQUIRE(restored.restoreJson(encoded.value()).ok());
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "4\\n";
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "5\\n";
    auto again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), encoded.value());
    auto fps  = restored.getInt("m_targetFPS");
    auto wind = restored.getFloat("m_pcgWindSpeed");
    REQUIRE(fps.ok());
    REQUIRE(wind.ok());
    CHECK_EQ(fps.value(), int64_t(144));
    CHECK_EQ(wind.value(), .875f);
    REQUIRE(restored.selectColor("m_sunColor").ok());
    CHECK_EQ(restored.getColorR(), 1.25f);
    CHECK_EQ(restored.getColorA(), 2.0f);
    const auto  before = again.value();
    std::string wrong  = before;
    auto        pos    = wrong.find("eve.ui.pcg-photo-mode-values");
    REQUIRE(pos != std::string::npos);
    wrong.replace(pos, 29, "eve.ui.wrong-photo-values");
    CHECK(!restored.restoreJson(wrong).ok());
    auto after = restored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before);
}
}  // namespace test_pcg_photo_mode_session
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_reflection_probe_culling)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <glm/glm.hpp>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include "PathBesideSource.h"
#include "common/ECS.h"
#include "graphics/ReflectionProbeCapture.h"
#include "graphics/ReflectionProbeRegistry.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// reflection_probe_culling.cpp
namespace test_reflection_probe_culling {
// reflection_probe_culling.cpp
using namespace eve::graphics;


// reflection_probe_quality.cpp
namespace {

constexpr float kPi = 3.14159265358979323846f;

[[maybe_unused]] float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

[[maybe_unused]] glm::vec3 faceDirection(int face, float u, float v) {
    switch (face) {
        case 0: return glm::normalize(glm::vec3(1.f, -v, -u));
        case 1: return glm::normalize(glm::vec3(-1.f, -v, u));
        case 2: return glm::normalize(glm::vec3(u, 1.f, v));
        case 3: return glm::normalize(glm::vec3(u, -1.f, -v));
        case 4: return glm::normalize(glm::vec3(u, -v, 1.f));
        default: return glm::normalize(glm::vec3(-u, -v, -1.f));
    }
}

[[maybe_unused]] std::array<glm::vec3, 24 * 33> allFaceEdges() {
    std::array<glm::vec3, 24 * 33> result{};
    size_t                         out = 0;
    for (int face = 0; face < 6; ++face) {
        for (int edge = 0; edge < 4; ++edge) {
            for (int sample = 0; sample < 33; ++sample) {
                const float t = float(sample) / 16.f - 1.f;
                const float u = edge < 2 ? (edge == 0 ? -1.f : 1.f) : t;
                const float v = edge >= 2 ? (edge == 2 ? -1.f : 1.f) : t;
                result[out++] = faceDirection(face, u, v);
            }
        }
    }
    return result;
}

}  // namespace


TEST_CASE("graphics.reflectionProbe.diffuseWhiteFurnaceNormalization") {
    // Cosine-distributed hemisphere samples estimate irradiance / PI directly.
    // For L(z) = 0.5 + 0.5z, E_cosine[z] = 2/3, hence the analytic result is 5/6.
    constexpr int sampleCount = 512;
    float         estimate    = 0.f;
    for (int i = 0; i < sampleCount; ++i) {
        const float     xiX    = float(i) / float(sampleCount);
        const float     xiY    = radicalInverse(uint32_t(i));
        const float     phi    = 2.f * kPi * xiX;
        const float     radius = std::sqrt(xiY);
        const glm::vec3 l(std::cos(phi) * radius, std::sin(phi) * radius, std::sqrt(std::max(1.f - xiY, 0.f)));
        estimate += 0.5f + 0.5f * l.z;
    }
    estimate /= float(sampleCount);
    CHECK(std::abs(estimate - 5.f / 6.f) < 0.003f);

    const std::string_view wgsl(eve::graphics::shaders::kReflectionProbeFilterWgsl);
    CHECK(wgsl.find("let sampleWeight = select(noL, 1.0, params.data.z > 0.5)") != std::string_view::npos);

    const std::string glslPath = (eve_test_path::sourceBesideTestDir(__FILE__).substr(
                                      0, eve_test_path::sourceBesideTestDir(__FILE__).find_last_of("/\\")) +
                                  "/../src/modules/graphics/shaders/reflection_probe_filter.comp");
    std::ifstream     glslFile(glslPath);
    REQUIRE(glslFile.good());
    const std::string glsl((std::istreambuf_iterator<char>(glslFile)), std::istreambuf_iterator<char>());
    CHECK(glsl.find("params.diffuseMode != 0u ? 1.0 : noL") != std::string::npos);
}
}  // namespace test_reflection_probe_culling
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_remotesync)
#include "PrivateTestScope.h"
#include <Poco/Exception.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/HotReload.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// remotesync.cpp
namespace test_remotesync {
namespace {

eve::filesystem::Filesystem *fs() { return eve::filesystem::Filesystem::create(); }

void useIdentity(const char *id) {
    auto *f = fs();
    REQUIRE(f->setIdentity(id, true));
    REQUIRE(f->setupWriteDirectory());
}

// Minimal HTTP file server matching the `eve dev` protocol:
//   GET /manifest → JSON [{path,size,mtime}, ...]
//   GET /raw/<rel> → file bytes
class TestHttpServer {
public:
    ~TestHttpServer() { stop(); }

    bool start(std::string root) {
        try {
            server_ = std::make_unique<Poco::Net::ServerSocket>(Poco::Net::SocketAddress(0));
            port_   = static_cast<uint16_t>(server_->address().port());
        } catch (...) {
            return false;
        }
        root_ = std::move(root);
        running_.store(true);
        thread_ = std::thread([this]() { loop(); });
        return true;
    }

    void stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        running_.store(false);
        // On Linux, closing a socket does NOT wake a thread blocked in accept().
        // A self-connection is the reliable cross-platform way to unblock it.
        try {
            Poco::Net::StreamSocket wake(Poco::Net::SocketAddress("127.0.0.1", port_));
        } catch (...) {
        }
        if (thread_.joinable()) thread_.join();
    }

    uint16_t port() const { return port_; }

private:
    void loop() {
        while (running_.load()) {
            Poco::Net::StreamSocket sock;
            try {
                sock = server_->acceptConnection();
            } catch (...) {
                if (!running_.load()) break;
                continue;
            }
            // Swallow any per-connection error (e.g. receiveBytes timeout on a
            // half-open client) so a bad request cannot crash the server thread.
            try {
                handle(sock);
            } catch (...) {
            }
            try {
                sock.close();
            } catch (...) {
            }
        }
    }

    void handle(Poco::Net::StreamSocket &sock) {
        sock.setReceiveTimeout(Poco::Timespan(2, 0));
        std::string req;
        char        buf[4096];
        while (req.find("\r\n\r\n") == std::string::npos) {
            int n = sock.receiveBytes(buf, sizeof(buf));
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
        }

        std::string path;
        if (!req.empty()) {
            const size_t sp1 = req.find(' ');
            const size_t sp2 = req.find(' ', sp1 + 1);
            if (sp1 != std::string::npos && sp2 != std::string::npos) path = req.substr(sp1 + 1, sp2 - sp1 - 1);
        }

        std::string body;
        int         status = 404;
        if (path == "/manifest") {
            body   = manifest();
            status = 200;
        } else if (path.rfind("/raw/", 0) == 0) {
            const std::string real = root_ + "/" + path.substr(5);
            std::error_code   ec;
            if (std::filesystem::is_regular_file(real, ec)) {
                std::ifstream      ifs(real, std::ios::binary);
                std::ostringstream oss;
                oss << ifs.rdbuf();
                body   = oss.str();
                status = 200;
            }
        }

        std::ostringstream head;
        head << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Not Found") << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n";
        const std::string out = head.str() + body;
        try {
            sock.sendBytes(out.data(), static_cast<int>(out.size()));
        } catch (...) {
        }
        try {
            sock.close();
        } catch (...) {
        }
    }

    std::string manifest() {
        std::ostringstream oss;
        oss << "[";
        bool            first = true;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
             it != std::filesystem::recursive_directory_iterator() && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                const std::string rel = std::filesystem::relative(it->path(), root_).generic_string();
                if (rel == ".eve-manifest.json") continue;
                const auto mt = std::filesystem::last_write_time(it->path(), ec);
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!first) oss << ",";
                first = false;
                oss << "{\"path\":\"" << rel << "\",\"size\":" << static_cast<long long>(it->file_size(ec))
                    << ",\"mtime\":"
                    << static_cast<long long>(
                           std::chrono::duration_cast<std::chrono::seconds>(mt.time_since_epoch()).count())
                    << "}";
            }
        }
        oss << "]";
        return oss.str();
    }

    std::string                              root_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::thread                              thread_;
    std::atomic<bool>                        running_{false};
    uint16_t                                 port_ = 0;
};

std::filesystem::path tempDir(const char *tag) {
    const auto      p = std::filesystem::temp_directory_path() / tag;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

void writeFile(const std::filesystem::path &p, const std::string &content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << content;
}

bool waitFor(const std::function<bool()> &cond, int ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

}  // namespace

TEST_CASE("hotreload.mountRealDirectoryShadow") {
    useIdentity("ev_ut_hot_overlay");
    auto *f = fs();

    const char *name   = "overlay_test.nut";
    const char *bundle = "bundle-version";
    f->write(name, bundle, std::strlen(bundle));

    const auto overlay = tempDir("eve_ut_hot_overlay");
    writeFile(overlay / name, "overlay-version");

    REQUIRE(f->mountRealDirectory(overlay.string(), "/", false));
    eve::filesystem::FileData *fd = f->read(name);
    REQUIRE(fd != nullptr);
    const std::string shadowed(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(shadowed == "overlay-version");
    delete fd;

    REQUIRE(f->unmountRealDirectory(overlay.string()));
    fd = f->read(name);
    REQUIRE(fd != nullptr);
    const std::string back(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(back == "bundle-version");
    delete fd;

    std::error_code ec;
    std::filesystem::remove_all(overlay, ec);
}

TEST_CASE("hotreload.remoteSyncDownload") {
    useIdentity("ev_ut_hot_remote");
    auto *f = fs();

    const auto root = tempDir("eve_ut_hot_remote_root");
    writeFile(root / "main.nut", "print('main v1')\n");
    writeFile(root / "scripts" / "a.nut", "a <- 1\n");
    writeFile(root / "fx.json", R"({"emissionRate":5,"particleLifetime":[1,1]})");

    TestHttpServer server;
    REQUIRE(server.start(root.string()));

    const auto overlay = tempDir("eve_ut_hot_remote_hot");

    auto *hot = eve::filesystem::HotReload::create();
    hot->setRemoteHotDir(overlay.string());
    REQUIRE(hot->startRemoteSync("http://127.0.0.1:" + std::to_string(server.port()), 60));

    REQUIRE(waitFor([&]() { return hot->remoteSyncStatus() == "synced"; }));

    // All three files should be queued as changes.
    std::vector<std::string> changes;
    while (true) {
        const std::string p = hot->pollRemoteChange();
        if (p.empty()) break;
        changes.push_back(p);
    }
    std::sort(changes.begin(), changes.end());
    REQUIRE(changes.size() == 3);
    CHECK(changes[0] == "fx.json");
    CHECK(changes[1] == "main.nut");
    CHECK(changes[2] == "scripts/a.nut");

    // Files landed in the overlay dir.
    REQUIRE(std::filesystem::is_regular_file(overlay / "main.nut"));
    REQUIRE(std::filesystem::is_regular_file(overlay / "scripts" / "a.nut"));
    REQUIRE(std::filesystem::is_regular_file(overlay / "fx.json"));

    // Overlay is mounted at "/" so the virtual FS resolves the synced copy.
    eve::filesystem::FileData *fd = f->read("main.nut");
    REQUIRE(fd != nullptr);
    const std::string synced(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(synced.find("v1") != std::string::npos);
    delete fd;

    // Touch main.nut on the server → a new change event after the next poll.
    // Use a different length so the manifest (size + second-granularity mtime)
    // reliably reports a change even when both edits land in the same second.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    writeFile(root / "main.nut", "print('main v2 -- longer edit')\n");
    REQUIRE(waitFor([&]() {
        while (true) {
            const std::string p = hot->pollRemoteChange();
            if (p.empty()) return false;
            if (p == "main.nut") return true;
        }
    }));

    fd = f->read("main.nut");
    REQUIRE(fd != nullptr);
    const std::string v2(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(v2.find("v2") != std::string::npos);
    delete fd;

    hot->stopRemoteSync();
    f->unmountRealDirectory(overlay.string());
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::remove_all(overlay, ec);
}

TEST_CASE("hotreload.remoteSyncUnreachableRecovers") {
    useIdentity("ev_ut_hot_remote_unreach");
    const auto overlay = tempDir("eve_ut_hot_remote_unreach_hot");

    auto *hot = eve::filesystem::HotReload::create();
    hot->setRemoteHotDir(overlay.string());
    // Nothing listening on this port → sync should start but stay not-synced.
    REQUIRE(hot->startRemoteSync("http://127.0.0.1:1", 50));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(hot->remoteSyncStatus() != "synced");
    hot->stopRemoteSync();

    std::error_code ec;
    std::filesystem::remove_all(overlay, ec);
}
}  // namespace test_remotesync
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_classic)
#include "PrivateTestScope.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include "PathBesideSource.h"
#include "common/Runtime.h"
#include "dialogue/DialogueSequence.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "i18n/I18n.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "map/MapObjectContract.h"
#include "map/Pathfinder.h"
#include "map/TileConfig.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/Quest.h"
#include "rpg/RpgDialect.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_classic.cpp
namespace test_rpg_classic {
TEST_CASE("rpg.classic.mainScriptCompilesThroughEveScriptFrontend") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "rpg-classic" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    const std::string script = source.str();

    CHECK(script.find("const SAVE_SLOT_COUNT = 3") != std::string::npos);
    CHECK(script.find("function inspectSaveSlot(slot)") != std::string::npos);
    CHECK(script.find("function migrateLegacySingleSlot()") != std::string::npos);
    CHECK(script.find("loadCheckpoint(true)") != std::string::npos);
    CHECK(script.find("newRunBeforeFirstSave") != std::string::npos);
    CHECK(script.find("const SETTINGS_SCHEMA = \"rpg-classic.settings.v2\"") != std::string::npos);
    CHECK(script.find("const LEGACY_SETTINGS_SCHEMA = \"rpg-classic.settings.v1\"") != std::string::npos);
    CHECK(script.find("function selectProductLanguage(language)") != std::string::npos);
    CHECK(script.find("dialogueFlow.validateLocalization(text, language)") != std::string::npos);
    CHECK(script.find("function validateProductLocalization()") != std::string::npos);
    CHECK(script.find("function openPauseMenu()") != std::string::npos);
    CHECK(script.find("function confirmDeleteSaveSlot()") != std::string::npos);
    CHECK(script.find("function confirmReturnToTitle()") != std::string::npos);
    CHECK(script.find("saveFs.writeTextAtomic(SETTINGS_PATH") != std::string::npos);
    CHECK(script.find("const SAVE_CONTENT_VERSION = \"rpg-classic.content.v10\"") != std::string::npos);
    CHECK(script.find("function loadWorldMap(mapId, x, y, fx, fy)") != std::string::npos);
    CHECK(script.find("function transitionThroughPortal(index)") != std::string::npos);
    CHECK(script.find("function beginQuestNpcDialogue(index)") != std::string::npos);
    CHECK(script.find("function openLootObject(index)") != std::string::npos);
    CHECK(script.find("function claimQuestRewards(questId)") != std::string::npos);
    CHECK(script.find("rpg.replaceQuestsFromJson(questsJson)") != std::string::npos);
    CHECK(script.find("rpg.replaceEncountersFromJson(encountersJson)") != std::string::npos);
    CHECK(script.find("rpg.newEncounterMemberActor(activeEncounterId, memberIndex)") != std::string::npos);
    CHECK(script.find("function selectNextEnemy()") != std::string::npos);
    CHECK(script.find("rpg.settleEncounterVictory(") != std::string::npos);
    CHECK(script.find("previous catalogue retained") != std::string::npos);
    CHECK(script.find("function validateWorldContentLinks()") != std::string::npos);
    CHECK(script.find("dialogueFlow.hasConversation(conversationId)") != std::string::npos);
    CHECK(script.find("rpg.hasQuestDefinition(questId)") != std::string::npos);
    CHECK(script.find("persist acceptedQuestContent = null") != std::string::npos);
    CHECK(script.find("function publishNarrativeContentPackage(reload = false)") != std::string::npos);
    CHECK(script.find(
              "function restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,") !=
          std::string::npos);
    CHECK(script.find("rpg.replaceStoriesFromDnut(storyDnut, \"data/stories.dnut\")") != std::string::npos);
    CHECK(script.find("text.replaceBundleFromJson(source)") != std::string::npos);
    CHECK(script.find("dialogueFlow.validateLocalization(text, \"zh-CN\")") != std::string::npos);
    CHECK(script.find("function resumePendingStoryEvent()") != std::string::npos);
    CHECK(script.find("rpg.newStorySession()") != std::string::npos);
    CHECK(script.find("function presentStoryStep()") != std::string::npos);
    CHECK(script.find("accepted content restored") != std::string::npos);
    CHECK(script.find("publishNarrativeContentPackage(true)") != std::string::npos);
    CHECK(script.find("loadFromFileWithObjectContract(path, worldObjectContract)") != std::string::npos);
    CHECK(script.find("allowCompatibleContentVersion(PREVIOUS_SAVE_CONTENT_VERSION)") != std::string::npos);
    CHECK(script.find("addQuestAdditionMigration(LEGACY_QUEST_CONTENT_VERSION") != std::string::npos);
    CHECK(script.find("worldState.consumeObject(worldMapId, map.getObjectName(index))") != std::string::npos);
    CHECK(script.find("save.mapCode") != std::string::npos);

    eve::Runtime runtime(2048, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.compileSource(script, "examples/rpg-classic/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}

TEST_CASE("rpg.classic.villageSupportsExplorationInteractionAndThreeEncounters") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path mapPath = sourceRoot / "examples" / "rpg-classic" / "data" / "village.json";
    std::ifstream               input(mapPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    std::vector<eve::map::MapObject> objects;
    std::string                      error;
    auto                             layers = eve::map::loadMapText(source.str(), &objects, &error);
    CHECK(error.empty());
    REQUIRE_EQ(layers.size(), std::size_t(1));
    auto *layer = layers.front();
    REQUIRE(layer != nullptr);
    CHECK_EQ(layer->getMapWidth(), 20);
    CHECK_EQ(layer->getMapHeight(), 12);

    eve::map::Pathfinder pathfinder(layer);
    pathfinder.blockGid(2);
    pathfinder.syncFromLayer();
    CHECK(pathfinder.isWalkable(2, 5));
    CHECK(!pathfinder.isWalkable(0, 5));
    CHECK(!pathfinder.isWalkable(9, 2));

    std::set<std::string> names;
    int                   spawnCount     = 0;
    int                   questNpcCount  = 0;
    int                   merchantCount  = 0;
    int                   lootCount      = 0;
    int                   encounterCount = 0;
    int                   portalCount    = 0;
    for (const auto &object : objects) {
        CHECK(names.emplace(object.name).second);
        if (object.type == "spawn")
            ++spawnCount;
        else if (object.type == "quest_npc")
            ++questNpcCount;
        else if (object.type == "merchant")
            ++merchantCount;
        else if (object.type == "loot")
            ++lootCount;
        else if (object.type == "portal")
            ++portalCount;
        else if (object.type == "encounter") {
            ++encounterCount;
            CHECK(pathfinder.isWalkable(static_cast<int>(object.x), static_cast<int>(object.y)));
        }
    }
    CHECK_EQ(spawnCount, 1);
    CHECK_EQ(questNpcCount, 1);
    CHECK_EQ(merchantCount, 1);
    CHECK_EQ(lootCount, 1);
    CHECK_EQ(encounterCount, 2);
    CHECK_EQ(portalCount, 1);

    eve::map::Map objectQueries;
    objectQueries.setObjects(objects);
    const auto elder = objectQueries.findObjectByName("elder");
    REQUIRE(elder.has_value());
    const int elderIndex = static_cast<int>(*elder);
    CHECK_EQ(objectQueries.getObjectType(elderIndex), std::string("quest_npc"));
    CHECK_EQ(objectQueries.getObjectProperty(elderIndex, "questId", ""), std::string("quest.slayer"));
    CHECK_EQ(objectQueries.getObjectProperty(elderIndex, "dialogueBase", ""), std::string("village.elder"));
    const auto encounter = objectQueries.findObjectAt(7.f, 6.f, "encounter");
    REQUIRE(encounter.has_value());
    CHECK_EQ(objectQueries.getObjectName(static_cast<int>(*encounter)), std::string("slime_west"));
    CHECK(!objectQueries.findObjectAt(7.f, 6.f, "merchant").has_value());

    const auto villageGate = objectQueries.findObjectByName("forest_gate");
    REQUIRE(villageGate.has_value());
    const int villageGateIndex = static_cast<int>(*villageGate);
    CHECK_EQ(objectQueries.getObjectPropertyCount(villageGateIndex), 5);
    CHECK(objectQueries.hasObjectProperty(villageGateIndex, "targetMap"));
    CHECK_EQ(objectQueries.getObjectProperty(villageGateIndex, "targetMap", ""), std::string("forest"));
    CHECK_EQ(objectQueries.getObjectProperty(villageGateIndex, "targetX", ""), std::string("2"));
    CHECK_EQ(objectQueries.getObjectProperty(villageGateIndex, "missing", "fallback"), std::string("fallback"));
    CHECK_EQ(objectQueries.getObjectPropertyName(villageGateIndex, 0), std::string("facingX"));

    const std::filesystem::path forestPath = sourceRoot / "examples" / "rpg-classic" / "data" / "forest.json";
    std::ifstream               forestInput(forestPath, std::ios::binary);
    REQUIRE(forestInput.is_open());
    std::ostringstream forestSource;
    forestSource << forestInput.rdbuf();
    std::vector<eve::map::MapObject> forestObjects;
    auto                             forestLayers = eve::map::loadMapText(forestSource.str(), &forestObjects, &error);
    CHECK(error.empty());
    REQUIRE_EQ(forestLayers.size(), std::size_t(1));

    const std::filesystem::path contractPath =
        sourceRoot / "examples" / "rpg-classic" / "data" / "world-object-contract.json";
    std::ifstream contractInput(contractPath, std::ios::binary);
    REQUIRE(contractInput.is_open());
    std::ostringstream contractSource;
    contractSource << contractInput.rdbuf();
    auto villageAdmission = eve::map::validateMapObjects(objects, contractSource.str());
    CHECK(villageAdmission.ok());
    auto forestAdmission = eve::map::validateMapObjects(forestObjects, contractSource.str());
    CHECK(forestAdmission.ok());

    eve::map::Map transactionalMap;
    auto          villageLoad = transactionalMap.loadFromTextWithObjectContract(source.str(), contractSource.str());
    REQUIRE(villageLoad.ok());
    CHECK(transactionalMap.findObjectByName("forest_gate").has_value());
    const std::string restrictiveContract =
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn","properties":[]}]})";
    auto rejectedForest = transactionalMap.loadFromTextWithObjectContract(forestSource.str(), restrictiveContract);
    CHECK(!rejectedForest.ok());
    CHECK(transactionalMap.findObjectByName("forest_gate").has_value());
    CHECK(!transactionalMap.findObjectByName("ranger").has_value());
    eve::map::Pathfinder forestPathfinder(forestLayers.front());
    forestPathfinder.blockGid(2);
    forestPathfinder.syncFromLayer();
    int forestEncounterCount = 0;
    int forestQuestNpcCount  = 0;
    int forestLootCount      = 0;
    for (const auto &object : forestObjects)
        if (object.type == "encounter")
            ++forestEncounterCount;
        else if (object.type == "quest_npc")
            ++forestQuestNpcCount;
        else if (object.type == "loot")
            ++forestLootCount;
    CHECK_EQ(encounterCount + forestEncounterCount, 3);
    CHECK_EQ(forestQuestNpcCount, 1);
    CHECK_EQ(forestLootCount, 1);
    CHECK(forestPathfinder.isWalkable(std::stoi(objectQueries.getObjectProperty(villageGateIndex, "targetX", "-1")),
                                      std::stoi(objectQueries.getObjectProperty(villageGateIndex, "targetY", "-1"))));

    eve::map::Map forestQueries;
    forestQueries.setObjects(forestObjects);
    const auto returnGate = forestQueries.findObjectByName("village_gate");
    REQUIRE(returnGate.has_value());
    const int returnGateIndex = static_cast<int>(*returnGate);
    CHECK_EQ(forestQueries.getObjectProperty(returnGateIndex, "targetMap", ""), std::string("village"));
    CHECK(pathfinder.isWalkable(std::stoi(forestQueries.getObjectProperty(returnGateIndex, "targetX", "-1")),
                                std::stoi(forestQueries.getObjectProperty(returnGateIndex, "targetY", "-1"))));
    const auto ranger = forestQueries.findObjectByName("ranger");
    REQUIRE(ranger.has_value());
    CHECK_EQ(forestQueries.getObjectProperty(static_cast<int>(*ranger), "questId", ""),
             std::string("quest.ranger_cache"));
    const auto cache = forestQueries.findObjectByName("forest_cache");
    REQUIRE(cache.has_value());
    const int cacheIndex = static_cast<int>(*cache);
    CHECK_EQ(forestQueries.getObjectProperty(cacheIndex, "requiredQuest", ""), std::string("quest.ranger_cache"));
    CHECK_EQ(forestQueries.getObjectProperty(cacheIndex, "notifyTopic", ""), std::string("interact"));
    CHECK_EQ(forestQueries.getObjectProperty(cacheIndex, "notifyTarget", ""), std::string("forest_cache"));
}

TEST_CASE("rpg.classic.elderDialogueHasOfferReminderTurnInAndCompletionRoutes") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path dialoguePath =
        sourceRoot / "examples" / "rpg-classic" / "data" / "village-dialogue.dnut";
    std::ifstream input(dialoguePath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    eve::dnut::StepKindRegistry dialogueRegistry;
    eve::dialogue::registerDialogueSequenceSteps(dialogueRegistry).expect("dialogue sequence vocabulary");
    auto compiled = eve::dnut::compileDnutConversations(source.str(), dialoguePath.string(), dialogueRegistry);
    REQUIRE(!compiled.hasErrors());
    auto assets = std::move(compiled.assets);
    CHECK_EQ(assets.size(), std::size_t(9));

    const auto findAsset = [&](const std::string &id) -> const eve::dnut::SequenceAsset * {
        for (const auto &asset : assets)
            if (asset.id == id) return &asset;
        return nullptr;
    };
    const auto *offer        = findAsset("village.elder.offer");
    const auto *turnIn       = findAsset("village.elder.turnin");
    const auto *rangerOffer  = findAsset("forest.ranger.offer");
    const auto *rangerTurnIn = findAsset("forest.ranger.turnin");
    REQUIRE(offer != nullptr);
    REQUIRE(turnIn != nullptr);
    REQUIRE(rangerOffer != nullptr);
    REQUIRE(rangerTurnIn != nullptr);
    const auto *offerChoice  = offer->findNode("decision");
    const auto *turnInChoice = turnIn->findNode("decision");
    REQUIRE(offerChoice != nullptr);
    REQUIRE(turnInChoice != nullptr);
    REQUIRE_EQ(offerChoice->routes.size(), std::size_t(2));
    REQUIRE_EQ(turnInChoice->routes.size(), std::size_t(2));
    CHECK_EQ(offerChoice->routes[0].label, std::string("accept"));
    CHECK_EQ(offerChoice->routes[1].label, std::string("later"));
    CHECK_EQ(turnInChoice->routes[0].label, std::string("claim"));
    CHECK_EQ(turnInChoice->routes[1].label, std::string("later"));
    REQUIRE(rangerOffer->findNode("decision") != nullptr);
    REQUIRE(rangerTurnIn->findNode("decision") != nullptr);

    eve::dnut::SequenceRuntime runner;
    runner.setStepRegistry(&dialogueRegistry);
    CHECK(runner.start(offer).ok());
    CHECK_EQ(runner.currentNodeId(), std::string("intro"));
    CHECK(runner.advance().ok());
    CHECK_EQ(runner.currentNodeId(), std::string("decision"));
    CHECK(runner.select("accept").ok());
    CHECK_EQ(runner.currentNodeId(), std::string("accepted"));
    CHECK(runner.advance().ok());
    CHECK(!runner.isActive());
}

TEST_CASE("rpg.classic.forestSideQuestHasDataDrivenObjectiveAndRewards") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path questPath = sourceRoot / "examples" / "rpg-classic" / "data" / "quests.json";
    std::ifstream               input(questPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    eve::rpg::QuestRegistry::clear();
    REQUIRE_EQ(eve::rpg::QuestRegistry::loadFromJson(source.str()), 2);
    const auto *definition = eve::rpg::QuestRegistry::find("quest.ranger_cache");
    REQUIRE(definition != nullptr);
    REQUIRE_EQ(definition->objectives.size(), std::size_t(1));
    CHECK_EQ(definition->objectives[0].topic, std::string("interact"));
    CHECK_EQ(definition->objectives[0].target, std::string("forest_cache"));
    REQUIRE_EQ(definition->rewards.size(), std::size_t(2));
    CHECK_EQ(definition->rewards[0].type, std::string("item"));
    CHECK_EQ(definition->rewards[0].id, std::string("potion"));
    CHECK_EQ(definition->rewards[1].type, std::string("attribute"));
    CHECK_EQ(definition->rewards[1].id, std::string("gold"));

    eve::rpg::Tracker tracker;
    CHECK(tracker.activate("quest.ranger_cache"));
    tracker.notify("interact", "forest_cache", 1);
    CHECK_EQ(tracker.getState("quest.ranger_cache"), std::string("ready"));
    CHECK(tracker.claim("quest.ranger_cache"));
    CHECK_EQ(tracker.getState("quest.ranger_cache"), std::string("completed"));
    eve::rpg::QuestRegistry::clear();
}

TEST_CASE("rpg.classic.mapsQuestsAndDialogueComposeWithoutDanglingReferences") {
    const std::filesystem::path root =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path() / "examples" /
        "rpg-classic" / "data";
    const auto readFile = [&](const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        std::ostringstream source;
        source << input.rdbuf();
        return source.str();
    };

    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    REQUIRE_EQ(eve::rpg::SkillRegistry::loadFromJson(readFile(root / "skills.json")), 6);
    auto quests = eve::rpg::QuestRegistry::replaceFromJsonStrict(readFile(root / "quests.json"));
    REQUIRE(quests.ok());
    auto encounters = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(readFile(root / "encounters.json"));
    REQUIRE(encounters.ok());
    auto stories = eve::rpg::RpgStoryCatalogue::replaceFromDnutStrict(readFile(root / "stories.dnut"), "stories.dnut");
    REQUIRE(stories.ok());
    CHECK(eve::rpg::RpgStoryCatalogue::contains("forest.arrival"));

    eve::dnut::StepKindRegistry dialogueRegistry;
    eve::dialogue::registerDialogueSequenceSteps(dialogueRegistry).expect("dialogue sequence vocabulary");
    auto compiled = eve::dnut::compileDnutConversations(readFile(root / "village-dialogue.dnut"),
                                                        "village-dialogue.dnut", dialogueRegistry);
    REQUIRE(!compiled.hasErrors());
    auto  assets       = std::move(compiled.assets);
    auto *localization = eve::i18n::I18n::create();
    REQUIRE(localization != nullptr);
    localization->clear();
    auto localizationBundle = localization->replaceBundleFromJson(readFile(root / "localization.json"));
    REQUIRE(localizationBundle.ok());
    CHECK_EQ(localizationBundle.value(), 2);
    const std::vector<std::string> productKeys{
        "content.map.village.name",
        "content.map.forest.name",
        "content.item.potion.name",
        "content.item.iron_sword.name",
        "content.item.leather_armor.name",
        "content.quest.quest.slayer.title",
        "content.quest.quest.ranger_cache.title",
        "content.shop.potion.description",
        "content.skill.skill.fireball.name",
        "content.encounter.slime.forest.name",
        "content.encounterMember.slime.forest.forest.alpha.name",
        "ui.title.continue",
        "ui.adjust.offer",
        "ui.gameOver.summary",
    };
    for (const auto &language : {"zh-CN", "en"})
        for (const auto &key : productKeys) CHECK(localization->hasInLanguage(language, key));
    const std::string scriptSource = readFile(root.parent_path() / "main.nut");
    const std::regex  translationCall(R"rx(\btrp?\("([^"]+)")rx");
    for (std::sregex_iterator it(scriptSource.begin(), scriptSource.end(), translationCall), end; it != end; ++it) {
        const std::string key      = (*it)[1].str();
        auto              coverage = localization->validateKeyCoverage(key);
        CHECK(coverage.ok());
        if (coverage.ok()) CHECK_EQ(coverage.value(), 2);
    }
    const std::regex logCall(R"rx(\blogMessage\("([^"]+)")rx");
    for (std::sregex_iterator it(scriptSource.begin(), scriptSource.end(), logCall), end; it != end; ++it) {
        auto coverage = localization->validateKeyCoverage("gameplayLog." + (*it)[1].str());
        CHECK(coverage.ok());
        if (coverage.ok()) CHECK_EQ(coverage.value(), 2);
    }
    REQUIRE(localization->selectLanguage("zh-CN").ok());
    CHECK_EQ(localization->get("content.item.iron_sword.name"), std::string("铁剑"));
    REQUIRE(localization->selectLanguage("en").ok());
    CHECK_EQ(localization->get("content.item.iron_sword.name"), std::string("Iron Sword"));
    std::set<std::string> conversationIds;
    for (const auto &asset : assets) {
        conversationIds.insert(asset.id);
        for (const auto &node : asset.nodes) {
            const eve::Value *key = node.payload.find("i18n");
            if (!key || !key->isString() || key->asString().empty()) continue;
            CHECK(localization->hasInLanguage("zh-CN", key->asString()));
            CHECK(localization->hasInLanguage("en", key->asString()));
        }
    }
    CHECK(conversationIds.count("story.forest.arrival") == 1);
    CHECK(localization->validateKeyCoverage("gameplayLog.story.recorded").ok());

    eve::rpg::RpgStoryCatalogue::clear();

    const std::vector<std::string> states{"offer", "active", "turnin", "completed"};
    for (const auto &mapName : {"village.json", "forest.json"}) {
        std::vector<eve::map::MapObject> objects;
        std::string                      error;
        auto                             layers = eve::map::loadMapText(readFile(root / mapName), &objects, &error);
        REQUIRE(error.empty());
        REQUIRE(!layers.empty());
        for (const auto &object : objects) {
            const auto property = [&](const char *name) {
                const auto found = object.properties.find(name);
                return found == object.properties.end() ? std::string{} : found->second;
            };
            if (object.type == "quest_npc") {
                const auto questId      = property("questId");
                const auto dialogueBase = property("dialogueBase");
                CHECK(eve::rpg::QuestRegistry::contains(questId));
                for (const auto &state : states) CHECK(conversationIds.count(dialogueBase + "." + state) == 1);
            } else if (object.type == "loot") {
                const auto requiredQuest = property("requiredQuest");
                const bool validRequiredQuest =
                    requiredQuest.empty() || eve::rpg::QuestRegistry::contains(requiredQuest);
                CHECK(validRequiredQuest);
            } else if (object.type == "encounter") {
                CHECK(eve::rpg::EncounterCatalogue::find(property("encounterId")) != nullptr);
            }
        }
    }
    eve::rpg::QuestRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::SkillRegistry::clear();
    localization->clear();
}
}  // namespace test_rpg_classic
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_classic_playthrough)
#include "PrivateTestScope.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "dialogue/DialogueSequence.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "i18n/I18n.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "rpg/Battle.h"
#include "rpg/BattleTactics.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/QuestReward.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/RpgDialect.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/ShopTransaction.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
#include "rpg/WorldInteraction.h"
#include "rpg/WorldState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_classic_playthrough.cpp
namespace test_rpg_classic_playthrough {
namespace {

std::string readClassicContent(const std::filesystem::path &root, const char *name) {
    std::ifstream input(root / name, std::ios::binary);
    if (!input.is_open()) return {};
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

const eve::dnut::SequenceAsset *findConversation(const std::vector<eve::dnut::SequenceAsset> &assets,
                                                 const std::string                           &id) {
    for (const auto &asset : assets)
        if (asset.id == id) return &asset;
    return nullptr;
}

void acceptQuestThroughDialogue(const std::vector<eve::dnut::SequenceAsset> &assets, const std::string &conversationId,
                                eve::rpg::Tracker &tracker, const std::string &questId) {
    const auto *conversation = findConversation(assets, conversationId);
    REQUIRE(conversation != nullptr);
    eve::dnut::StepKindRegistry registry;
    eve::dialogue::registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");
    eve::dnut::SequenceRuntime runner;
    runner.setStepRegistry(&registry);
    REQUIRE(runner.start(conversation).ok());
    REQUIRE(runner.advance().ok());
    REQUIRE_EQ(runner.currentNodeId(), std::string("decision"));
    REQUIRE(runner.select("accept").ok());
    REQUIRE(tracker.activate(questId));
}

void winEncounter(eve::rpg::Party &party, eve::rpg::RPGActor &hero, eve::rpg::RPGActor &companion,
                  eve::rpg::Tracker &tracker, eve::rpg::GameState &gameState, const std::string &mapId,
                  const std::string &objectId, const std::string &encounterId) {
    eve::rpg::Battle battle;
    auto             composed = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    REQUIRE(composed.ok());
    REQUIRE_EQ(composed.value(), 2);
    std::vector<eve::rpg::RPGActor *> enemies;
    const int                         memberCount = eve::rpg::EncounterCatalogue::memberCount(encounterId);
    REQUIRE(memberCount > 0);
    for (int memberIndex = 0; memberIndex < memberCount; ++memberIndex) {
        auto *enemy = eve::rpg::EncounterCatalogue::createMemberActor(encounterId, memberIndex);
        REQUIRE(enemy != nullptr);
        enemies.push_back(enemy);
        battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    }
    while (!battle.isFinished()) {
        if (!hero.isDead("hp")) REQUIRE(battle.setActionChecked(&hero, "", nullptr).ok());
        if (!companion.isDead("hp"))
            REQUIRE(eve::rpg::BattleTacticsCatalogue::queueAction(&battle, &companion, "companion.ranger").ok());
        battle.autoEnemyActions();
        battle.startRound();
        while (battle.executeNextAction() && !battle.isFinished()) {
        }
    }
    REQUIRE(battle.isVictory());
    eve::rpg::RPG rpg;
    auto          settled = rpg.settleEncounterVictory(&hero, &tracker, &gameState, encounterId, mapId, objectId);
    REQUIRE(settled.ok());
    for (auto *enemy : enemies) enemy->release();
}

}  // namespace

TEST_CASE("rpg.classic.playthroughCompletesBothQuestsAndRestoresCheckpoint") {
    const std::filesystem::path contentRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path() / "examples" /
        "rpg-classic" / "data";

    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    eve::inventory::ItemRegistry::clear();
    REQUIRE_EQ(eve::rpg::SkillRegistry::loadFromJson(readClassicContent(contentRoot, "skills.json")), 6);
    auto quests = eve::rpg::QuestRegistry::replaceFromJsonStrict(readClassicContent(contentRoot, "quests.json"));
    REQUIRE(quests.ok());
    auto encounters =
        eve::rpg::EncounterCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "encounters.json"));
    REQUIRE(encounters.ok());
    REQUIRE_EQ(encounters.value(), 3);
    auto tactics =
        eve::rpg::BattleTacticsCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "battle-tactics.json"));
    REQUIRE(tactics.ok());
    auto stories = eve::rpg::RpgStoryCatalogue::replaceFromDnutStrict(readClassicContent(contentRoot, "stories.dnut"),
                                                                      "stories.dnut");
    REQUIRE(stories.ok());
    REQUIRE_EQ(stories.value(), 1);
    REQUIRE_EQ(eve::rpg::EncounterCatalogue::memberCount("slime.forest"), 2);
    REQUIRE_EQ(eve::inventory::ItemRegistry::loadFromJson(readClassicContent(contentRoot, "items.json")), 3);
    auto shopCatalogue = eve::rpg::ShopCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "shop.json"));
    REQUIRE(shopCatalogue.ok());

    eve::dnut::StepKindRegistry dialogueRegistry;
    eve::dialogue::registerDialogueSequenceSteps(dialogueRegistry).expect("dialogue sequence vocabulary");
    auto compiled = eve::dnut::compileDnutConversations(readClassicContent(contentRoot, "village-dialogue.dnut"),
                                                        "village-dialogue.dnut", dialogueRegistry);
    REQUIRE(!compiled.hasErrors());
    auto  conversations = std::move(compiled.assets);
    auto *localization  = eve::i18n::I18n::create();
    REQUIRE(localization != nullptr);
    localization->clear();
    auto localized = localization->replaceBundleFromJson(readClassicContent(contentRoot, "localization.json"));
    REQUIRE(localized.ok());
    for (const auto &conversation : conversations)
        for (const auto &node : conversation.nodes)
            if (const auto *key = node.payload.find("i18n"); key && key->isString())
                REQUIRE(localization->hasInLanguage("zh-CN", key->asString()));

    eve::rpg::GameState gameState;
    gameState.setVariable("gold", 0.0);
    gameState.setVariable("kills", 0.0);
    gameState.setVariable("statPoints", 0.0);
    gameState.setVariable("save.mapCode", 0.0);
    eve::rpg::WorldState world(gameState);
    eve::rpg::Tracker    tracker;
    eve::rpg::RPGActor  *hero      = eve::rpg::RPGActor::createActor();
    eve::rpg::RPGActor  *companion = eve::rpg::RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->setBaseAttribute("attack", 30.0);
    hero->setBaseAttribute("defense", 5.0);
    hero->setBaseAttribute("hp", 100.0);
    hero->setBaseAttribute("speed", 10.0);
    hero->setCurrent("hp", 100.0);
    companion->setBaseAttribute("attack", 16.0);
    companion->setBaseAttribute("defense", 8.0);
    companion->setBaseAttribute("hp", 80.0);
    companion->setBaseAttribute("speed", 12.0);
    companion->setCurrent("hp", 80.0);
    companion->learnSkill("skill.strike");
    companion->learnSkill("skill.ranger_aid");
    eve::rpg::Party party;
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion.ranger", companion).ok());
    eve::inventory::Bag          bag(12);
    eve::inventory::EquipmentSet equipment;
    equipment.setId("hero");
    equipment.defineSlot("weapon");
    equipment.defineSlot("armor");

    eve::rpg::RpgStorySession arrival;
    REQUIRE(arrival.begin("forest.arrival", &gameState, &party, &bag, &equipment).ok());
    REQUIRE_EQ(arrival.getStepKind(), std::string("dialogue"));
    CHECK_EQ(arrival.getStepPayload().find("id")->asString(), std::string("story.forest.arrival"));
    REQUIRE(findConversation(conversations, "story.forest.arrival") != nullptr);
    REQUIRE(arrival.advance().ok());
    REQUIRE_EQ(arrival.getStepKind(), std::string("wait"));
    CHECK(gameState.hasSelfString("story.forest.arrival", "cursor"));
    eve::rpg::RPGSaveSession eventSave;
    eventSave.bindParty(gameState, tracker, party, bag, equipment);
    REQUIRE(eventSave.setContentVersion("rpg-classic.content.v10").ok());
    auto eventCheckpoint = eventSave.snapshotJson();
    REQUIRE(eventCheckpoint.ok());
    REQUIRE(arrival.advance().ok());
    REQUIRE(eventSave.restoreSnapshotJson(eventCheckpoint.value()).ok());
    eve::rpg::RpgStorySession resumedArrival;
    REQUIRE(resumedArrival.begin("forest.arrival", &gameState, &party, &bag, &equipment).ok());
    CHECK_EQ(resumedArrival.getStepKind(), std::string("wait"));
    REQUIRE(resumedArrival.advance().ok());
    CHECK_EQ(resumedArrival.getStepKind(), std::string("message"));
    CHECK_EQ(resumedArrival.getStepPayload().find("text")->asString(), std::string("gameplayLog.story.recorded"));
    REQUIRE(resumedArrival.advance().ok());
    CHECK(!resumedArrival.isActive());
    CHECK(gameState.hasSelfVariable("story.forest.arrival", "completed"));
    CHECK_EQ(gameState.getSelfVariable("story.forest.arrival", "completed"), 1.0);
    CHECK(!gameState.hasSelfString("story.forest.arrival", "cursor"));

    acceptQuestThroughDialogue(conversations, "village.elder.offer", tracker, "quest.slayer");
    winEncounter(party, *hero, *companion, tracker, gameState, "village", "slime_west", "slime.west");
    winEncounter(party, *hero, *companion, tracker, gameState, "village", "slime_north", "slime.north");
    gameState.setVariable("save.mapCode", 1.0);
    winEncounter(party, *hero, *companion, tracker, gameState, "forest", "forest_slime", "slime.forest");
    REQUIRE_EQ(tracker.getState("quest.slayer"), std::string("ready"));
    auto mainRewards = eve::rpg::QuestReward::claim(tracker, gameState, bag, "quest.slayer");
    REQUIRE(mainRewards.ok());

    acceptQuestThroughDialogue(conversations, "forest.ranger.offer", tracker, "quest.ranger_cache");
    eve::rpg::WorldLootRequest cache;
    cache.mapId           = "forest";
    cache.objectId        = "forest_cache";
    cache.requiredQuestId = "quest.ranger_cache";
    cache.itemId          = "potion";
    cache.itemQuantity    = 1;
    cache.attributeId     = "gold";
    cache.attributeAmount = 10.0;
    cache.notifyTopic     = "interact";
    cache.notifyTarget    = "forest_cache";
    cache.notifyAmount    = 1;
    auto cacheLoot        = eve::rpg::WorldInteraction::collectLoot(gameState, tracker, bag, cache);
    REQUIRE(cacheLoot.ok());
    REQUIRE_EQ(tracker.getState("quest.ranger_cache"), std::string("ready"));
    auto sideRewards = eve::rpg::QuestReward::claim(tracker, gameState, bag, "quest.ranger_cache");
    REQUIRE(sideRewards.ok());
    auto boughtPotion = eve::rpg::ShopTransaction::buyOffer(gameState, bag, "gold", "potion", 1);
    REQUIRE(boughtPotion.ok());
    auto soldPotion = eve::rpg::ShopTransaction::sellOffer(gameState, bag, "gold", "potion", 1);
    REQUIRE(soldPotion.ok());

    REQUIRE_EQ(tracker.getState("quest.slayer"), std::string("completed"));
    REQUIRE_EQ(tracker.getState("quest.ranger_cache"), std::string("completed"));
    REQUIRE_EQ(gameState.getVariable("gold"), 115.0);
    REQUIRE_EQ(gameState.getVariable("kills"), 3.0);
    REQUIRE_EQ(gameState.getVariable("statPoints"), 3.0);
    REQUIRE_EQ(bag.countItem("potion"), 2);

    eve::rpg::RPGSaveSession save;
    save.bindParty(gameState, tracker, party, bag, equipment);
    REQUIRE(save.setContentVersion("rpg-classic.content.v10").ok());
    auto checkpoint = save.snapshotJson();
    REQUIRE(checkpoint.ok());

    gameState.setVariable("gold", 0.0);
    gameState.setVariable("kills", 0.0);
    gameState.setVariable("statPoints", 0.0);
    gameState.setVariable("save.mapCode", 0.0);
    bag.clear();
    hero->setCurrent("hp", 1.0);
    companion->setCurrent("hp", 1.0);
    REQUIRE(world.resetObject("forest", "forest_cache").ok());
    REQUIRE(save.restoreSnapshotJson(checkpoint.value()).ok());

    CHECK_EQ(gameState.getVariable("gold"), 115.0);
    CHECK_EQ(gameState.getVariable("kills"), 3.0);
    CHECK_EQ(gameState.getVariable("statPoints"), 3.0);
    CHECK_EQ(gameState.getVariable("save.mapCode"), 1.0);
    CHECK_EQ(tracker.getState("quest.slayer"), std::string("completed"));
    CHECK_EQ(tracker.getState("quest.ranger_cache"), std::string("completed"));
    CHECK_EQ(bag.countItem("potion"), 2);
    CHECK_EQ(hero->getCurrent("hp"), 100.0);
    CHECK(companion->getCurrent("hp") > 1.0);
    CHECK(world.isObjectConsumed("village", "slime_west"));
    CHECK(world.isObjectConsumed("village", "slime_north"));
    CHECK(world.isObjectConsumed("forest", "forest_slime"));
    CHECK(world.isObjectConsumed("forest", "forest_cache"));

    hero->release();
    companion->release();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::RpgStoryCatalogue::clear();
}
}  // namespace test_rpg_classic_playthrough
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor_llm)
#include "PrivateTestScope.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "tensor/AffineQuant.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/OnnxByteStorage.h"
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxModel.h"
#include "tensor/Optimizer.h"
#include "tensor/Quant.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tensor_llm.cpp
namespace test_tensor_llm {
// tensor_llm.cpp
using namespace eve::tensor;

namespace {

constexpr int kVocab   = 50257;
constexpr int kEmbDim  = 64;
constexpr int kHeads   = 16;
constexpr int kHeadDim = kEmbDim / kHeads;  // 4
constexpr int kLayers  = 8;
constexpr int kInner   = 256;
constexpr int kEos     = 50256;
constexpr int kGenToks = 24;

[[maybe_unused]] std::string assetPath(const std::string &name) {
    return std::string(EVENGINE_SOURCE_DIR) + "/test/assets/mini_llm/" + name;
}

struct Weight {
    std::vector<int>   dims;
    std::vector<float> data;
};

struct Model {
    std::map<std::string, Weight> weights;
    std::vector<std::string>      tokens;  // id -> decoded text
    std::vector<float>            prompt;
    std::vector<float>            reference;

    const Weight &w(const std::string &name) const { return weights.at(name); }
};

[[maybe_unused]] bool loadFile(const std::string &path, std::vector<char> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(n));
    f.read(out.data(), n);
    return !f.fail() || n == 0;
}

[[maybe_unused]] bool loadModel(Model &m) {
    std::vector<char> buf;
    if (!loadFile(assetPath("tinystories.bin"), buf)) return false;
    const char *p   = buf.data();
    const char *end = p + buf.size();
    if (end - p < 7 || std::memcmp(p, "EVLLM11", 7) != 0) {
        std::fprintf(stderr, "[llm] bad magic %.7s\n", p);
        return false;
    }
    p += 7;
    int count = 0;
    if (end - p < 4) return false;
    std::memcpy(&count, p, 4);
    p += 4;
    for (int i = 0; i < count; ++i) {
        int nameLen = 0;
        if (end - p < 4) return false;
        std::memcpy(&nameLen, p, 4);
        p += 4;
        if (end - p < nameLen) return false;
        std::string name(p, static_cast<size_t>(nameLen));
        p += nameLen;
        int rank = 0;
        if (end - p < 4) return false;
        std::memcpy(&rank, p, 4);
        p += 4;
        Weight wt;
        wt.dims.resize(static_cast<size_t>(rank));
        if (end - p < rank * 4) return false;
        std::memcpy(wt.dims.data(), p, static_cast<size_t>(rank) * 4);
        p += static_cast<size_t>(rank) * 4;
        long long bytes = 0;
        if (end - p < 8) return false;
        std::memcpy(&bytes, p, 8);
        p += 8;
        if (bytes <= 0 || end - p < bytes || (bytes % 4) != 0) return false;
        wt.data.resize(static_cast<size_t>(bytes) / 4);
        std::memcpy(wt.data.data(), p, static_cast<size_t>(bytes));
        p += bytes;
        m.weights[name] = std::move(wt);
    }

    if (!loadFile(assetPath("tokens.bin"), buf)) return false;
    p            = buf.data();
    end          = p + buf.size();
    int tokCount = 0;
    std::memcpy(&tokCount, p, 4);
    p += 4;
    m.tokens.reserve(static_cast<size_t>(tokCount));
    for (int i = 0; i < tokCount; ++i) {
        int len = 0;
        std::memcpy(&len, p, 4);
        p += 4;
        m.tokens.emplace_back(p, static_cast<size_t>(len));
        p += len;
    }
    return true;
}

[[maybe_unused]] std::vector<float> readCsvInts(const std::string &path) {
    std::vector<char> buf;
    if (!loadFile(path, buf)) return {};
    std::string        s(buf.begin(), buf.end());
    std::vector<float> out;
    size_t             i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(static_cast<float>(std::atoi(s.substr(i, j - i).c_str())));
        i = j + 1;
    }
    return out;
}

[[maybe_unused]] Tensor *toTensor(const std::vector<float> &data) {
    const int dims[1] = {static_cast<int>(data.size())};
    auto     *t       = new Tensor(dims, 1);
    for (size_t i = 0; i < data.size(); ++i) t->set(static_cast<int>(i), data[i]);
    return t;
}

[[maybe_unused]] Tensor *toTensor(const Weight &wt) {
    auto *t = new Tensor(wt.dims.data(), static_cast<int>(wt.dims.size()));
    for (size_t i = 0; i < wt.data.size(); ++i) t->set(static_cast<int>(i), wt.data[i]);
    return t;
}

/** Position ids [0..n) as a plain eager tensor (traceable as a constant). */
[[maybe_unused]] Tensor *arangeTensor(int n) {
    auto *t = new Tensor(n);
    for (int i = 0; i < n; ++i) t->set1(i, float(i));
    return t;
}

/** Causal mask [1, H, T, T]: 0 for s<=t, -1e4 for s>t. */
[[maybe_unused]] Tensor *causalMask(TF *tf, int T, int H) {
    const int dims[4] = {1, H, T, T};
    auto     *m       = new Tensor(dims, 4);
    m->fill(-1e4f);
    for (int h = 0; h < H; ++h)
        for (int t = 0; t < T; ++t)
            for (int s = 0; s <= t; ++s) m->set(((h * T + t) * T + s), 0.f);
    (void)tf;
    return m;
}

/**
 * GPT-2 hidden state after all transformer layers + final layernorm.
 * `ids` is [T] (eager float ids, or a traced placeholder).
 */
using WeightMap = std::map<std::string, Tensor *>;

[[maybe_unused]] WeightMap buildWeightTensors(const Model &m) {
    WeightMap out;
    for (const auto &kv : m.weights) out[kv.first] = toTensor(kv.second);
    return out;
}

[[maybe_unused]] const char *quantTag(DType dt) {
    switch (dt) {
        case DType::Fp16: return "fp16";
        case DType::Fp8E4M3: return "fp8";
        case DType::Fp4E2M1: return "fp4";
        case DType::Int8: return "int8";
        case DType::Int4: return "int4";
        default: return "float32";
    }
}

[[maybe_unused]] WeightMap buildQuantWeightTensors(TF *tf, const Model &m, DType dt, int group) {
    WeightMap out;
    for (const auto &kv : m.weights) {
        // Weight-only quantization: matrices (incl. embeddings / lm_head) are
        // packed; layernorm scales/biases and linear biases stay fp32 (they are
        // a tiny fraction of the bytes and keep the dequant path simple).
        const std::string &n      = kv.first;
        const bool         matrix = n.find(".bias") == std::string::npos && n.find("ln_") == std::string::npos;
        if (!matrix) {
            out[kv.first] = toTensor(kv.second);
            continue;
        }
        Tensor *t     = toTensor(kv.second);
        out[kv.first] = tf->quantizeWeight(t, quantTag(dt), group);
    }
    return out;
}

[[maybe_unused]] Tensor *buildHidden(TF *tf, Tensor *ids, const WeightMap &wm) {
    const int T   = ids->getSize();
    Tensor   *wte = wm.at("transformer.wte.weight");
    Tensor   *wpe = wm.at("transformer.wpe.weight");
    Tensor   *tok = tf->embedding(wte, ids);
    Tensor   *pos = arangeTensor(T);
    Tensor   *pe  = tf->embedding(wpe, pos);
    Tensor   *x   = tf->add(tok, pe);

    for (int i = 0; i < kLayers; ++i) {
        const std::string p    = "transformer.h." + std::to_string(i) + ".";
        Tensor           *ln1w = wm.at(p + "ln_1.weight");
        Tensor           *ln1b = wm.at(p + "ln_1.bias");
        Tensor           *h    = tf->layernormWB(x, ln1w, ln1b, 1e-5f);

        Tensor *qkv = tf->add(tf->matmul(h, wm.at(p + "attn.c_attn.weight")),
                              wm.at(p + "attn.c_attn.bias"));  // [T,3D]
        Tensor *q   = tf->slice(qkv, 1, 0, kEmbDim);
        Tensor *k   = tf->slice(qkv, 1, kEmbDim, 2 * kEmbDim);
        Tensor *v   = tf->slice(qkv, 1, 2 * kEmbDim, 3 * kEmbDim);
        q           = tf->reshape3(q, T, kHeads, kHeadDim);
        k           = tf->reshape3(k, T, kHeads, kHeadDim);
        v           = tf->reshape3(v, T, kHeads, kHeadDim);
        q           = tf->permute3(q, 1, 0, 2);  // [H,T,Dh]
        k           = tf->permute3(k, 1, 0, 2);
        v           = tf->permute3(v, 1, 0, 2);
        q           = tf->reshape4(q, 1, kHeads, T, kHeadDim);
        k           = tf->reshape4(k, 1, kHeads, T, kHeadDim);
        v           = tf->reshape4(v, 1, kHeads, T, kHeadDim);
        Tensor *att = tf->sdpaMasked(q, k, v, causalMask(tf, T, kHeads), 1.f / std::sqrt(float(kHeadDim)));
        att         = tf->permute4(tf->reshape4(att, 1, kHeads, T, kHeadDim), 0, 2, 1, 3);
        att         = tf->reshape2(att, T, kEmbDim);
        Tensor *out = tf->add(tf->matmul(att, wm.at(p + "attn.c_proj.weight")), wm.at(p + "attn.c_proj.bias"));
        x           = tf->add(x, out);

        h = tf->layernormWB(x, wm.at(p + "ln_2.weight"), wm.at(p + "ln_2.bias"), 1e-5f);
        h = tf->gelu(tf->add(tf->matmul(h, wm.at(p + "mlp.c_fc.weight")), wm.at(p + "mlp.c_fc.bias")));
        h = tf->add(tf->matmul(h, wm.at(p + "mlp.c_proj.weight")), wm.at(p + "mlp.c_proj.bias"));
        x = tf->add(x, h);
    }
    return tf->layernormWB(x, wm.at("transformer.ln_f.weight"), wm.at("transformer.ln_f.bias"), 1e-5f);
}

/** Full logits [T, vocab] (used for the numpy cross-check / compiled check). */
[[maybe_unused]] Tensor *fullLogits(TF *tf, Tensor *hidden, const WeightMap &wm) {
    Tensor *head = wm.at("lm_head.weight");
    return tf->matmul(hidden, tf->transpose(head));
}

[[maybe_unused]] int greedyNext(TF *tf, const std::vector<float> &ids, const WeightMap &wm) {
    Tensor   *in      = toTensor(ids);
    Tensor   *hidden  = buildHidden(tf, in, wm);
    Tensor   *last    = tf->slice(hidden, 0, static_cast<int>(ids.size()) - 1, static_cast<int>(ids.size()));
    Tensor   *logits1 = fullLogits(tf, last, wm);  // [1, vocab]
    Tensor   *am      = tf->argmax(logits1, 1, 0);
    const int token   = static_cast<int>(am->get(0));
    delete am;
    delete logits1;
    delete last;
    delete hidden;
    delete in;
    return token;
}

[[maybe_unused]] void softmaxRows(float *data, int rows, int cols) {
    for (int r = 0; r < rows; ++r) {
        float  mx  = -std::numeric_limits<float>::infinity();
        float *row = data + size_t(r) * cols;
        for (int c = 0; c < cols; ++c) mx = std::max(mx, row[c]);
        double sum = 0.0;
        for (int c = 0; c < cols; ++c) {
            row[c] = static_cast<float>(std::exp(double(row[c] - mx)));
            sum += row[c];
        }
        for (int c = 0; c < cols; ++c) row[c] = float(row[c] / sum);
    }
}

[[maybe_unused]] std::string decode(const Model &m, const std::vector<float> &ids) {
    std::string s;
    for (float f : ids) {
        const int id = static_cast<int>(f);
        if (id >= 0 && id < static_cast<int>(m.tokens.size())) s += m.tokens[static_cast<size_t>(id)];
    }
    return s;
}

[[maybe_unused]] Model *model() {
    static Model *m = [] {
        auto *mm = new Model();
        if (!loadModel(*mm)) {
            std::fprintf(stderr,
                         "[llm] skip: test/assets/mini_llm missing (see scripts/"
                         "export_tiny_gpt2.py)\n");
            delete mm;
            return static_cast<Model *>(nullptr);
        }
        return mm;
    }();
    return m;
}

[[maybe_unused]] bool ensureReference(Model &m) {
    if (m.reference.empty()) m.reference = readCsvInts(assetPath("reference_tokens.txt"));
    if (m.prompt.empty()) m.prompt = readCsvInts(assetPath("prompt.txt"));
    return !m.reference.empty() && !m.prompt.empty();
}

}  // namespace

TEST_CASE("tensor.llm.loadBundle") {
    Model *mp = model();
    if (!mp) return;
    const Model &m = *mp;
    CHECK_EQ(static_cast<int>(m.weights.size()), 101);
    const auto &wte = m.w("transformer.wte.weight");
    CHECK_EQ(wte.dims.size(), size_t(2));
    CHECK_EQ(wte.dims[0], kVocab);
    CHECK_EQ(wte.dims[1], kEmbDim);
    const auto &head = m.w("lm_head.weight");
    CHECK_EQ(head.dims[0], kVocab);
    const auto &ca = m.w("transformer.h.0.attn.c_attn.weight");
    CHECK_EQ(ca.dims[0], kEmbDim);
    CHECK_EQ(ca.dims[1], 3 * kEmbDim);
    CHECK_EQ(m.tokens.size(), size_t(kVocab));
}

TEST_CASE("tensor.llm.forwardMatchesNumpy") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    // Reference context: first 16 tokens of the numpy greedy generation.
    REQUIRE_GE(m.reference.size(), size_t(16));
    const std::vector<float> ctx(m.reference.begin(), m.reference.begin() + 16);

    std::vector<char> buf;
    REQUIRE(loadFile(assetPath("probs16.bin"), buf));
    REQUIRE_EQ(buf.size(), size_t(16 * kVocab * 4));

    auto     *tf     = TF::create();
    Tensor   *in     = toTensor(ctx);
    WeightMap wm     = buildWeightTensors(m);
    Tensor   *logits = fullLogits(tf, buildHidden(tf, in, wm), wm);
    REQUIRE_EQ(logits->getSize(), 16 * kVocab);
    std::vector<float> probs(logits->data(), logits->data() + logits->getSize());
    softmaxRows(probs.data(), 16, kVocab);

    const float *ref       = reinterpret_cast<const float *>(buf.data());
    float        maxDiff   = 0.f;
    int          top1Match = 0;
    for (int r = 0; r < 16; ++r) {
        int engineArgmax = 0, refArgmax = 0;
        for (int c = 0; c < kVocab; ++c) {
            const float e = probs[static_cast<size_t>(r) * kVocab + c];
            const float g = ref[static_cast<size_t>(r) * kVocab + c];
            maxDiff       = std::max(maxDiff, std::fabs(e - g));
            if (e > probs[static_cast<size_t>(r) * kVocab + engineArgmax]) engineArgmax = c;
            if (g > ref[static_cast<size_t>(r) * kVocab + refArgmax]) refArgmax = c;
        }
        if (engineArgmax == refArgmax) ++top1Match;
    }
    std::fprintf(stderr, "[llm] eager-vs-numpy max prob diff = %.3e, top-1 agreement %d/16\n", maxDiff, top1Match);
    CHECK(maxDiff < 1e-4f);
    CHECK_GE(top1Match, 14);

    delete logits;
    delete in;
}

TEST_CASE("tensor.llm.compiledMatchesEager") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    REQUIRE_GE(m.reference.size(), size_t(16));
    const std::vector<float> ctx(m.reference.begin(), m.reference.begin() + 16);

    auto     *tf          = TF::create();
    Tensor   *in          = toTensor(ctx);
    WeightMap wm          = buildWeightTensors(m);
    Tensor   *eagerLogits = fullLogits(tf, buildHidden(tf, in, wm), wm);

    auto   *fn = tf->func();
    Tensor *ph = fn->input1(16);
    fn->setOutput(fullLogits(tf, buildHidden(tf, ph, wm), wm));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    Tensor                           *out = compiled->run1(in);

    std::fprintf(stderr, "[llm] compiled device = %s\n", compiled->getDevice().c_str());
    float maxDiff = 0.f;
    for (int i = 0; i < out->getSize(); ++i) maxDiff = std::max(maxDiff, std::fabs(out->get(i) - eagerLogits->get(i)));
    std::fprintf(stderr, "[llm] compiled-vs-eager max logit diff = %.3e\n", maxDiff);
    CHECK(maxDiff < 1e-4f);

    delete out;
    delete in;
    delete eagerLogits;
}

TEST_CASE("tensor.llm.generate") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    REQUIRE_GE(m.reference.size(), m.prompt.size() + size_t(kGenToks));
    REQUIRE_GT(m.prompt.size(), size_t(0));

    auto              *tf      = TF::create();
    WeightMap          wm      = buildWeightTensors(m);
    std::vector<float> gen     = m.prompt;
    const auto         t0      = std::chrono::steady_clock::now();
    int                matches = 0;
    const size_t       refOff  = m.prompt.size();  // reference includes the prompt prefix
    for (int i = 0; i < kGenToks; ++i) {
        const int tok = greedyNext(tf, gen, wm);
        gen.push_back(float(tok));
        if (tok == static_cast<int>(m.reference[refOff + static_cast<size_t>(i)])) ++matches;
        if (tok == kEos) break;
    }
    const double msPerTok = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
                            static_cast<double>(gen.size() - m.prompt.size());

    std::fprintf(stderr,
                 "[llm] generated %zu tokens (%.1f ms/token), greedy match vs numpy "
                 "%d/%d\n",
                 gen.size() - m.prompt.size(), msPerTok, matches, kGenToks);
    std::fprintf(stderr, "[llm] prompt:  %s\n", decode(m, m.prompt).c_str());
    std::fprintf(stderr, "[llm] engine:  %s\n", decode(m, gen).c_str());
    std::fprintf(stderr, "[llm] numpy:   %s\n",
                 decode(m, std::vector<float>(m.reference.begin() + ptrdiff_t(refOff),
                                              m.reference.begin() + ptrdiff_t(refOff) + kGenToks))
                     .c_str());
    CHECK_GE(matches, static_cast<int>(kGenToks * 0.8));
}

TEST_CASE("tensor.llm.quantizedDtypes") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    REQUIRE_GE(m.reference.size(), size_t(16));
    const std::vector<float> ctx(m.reference.begin(), m.reference.begin() + 16);

    auto              *tf   = TF::create();
    Tensor            *in   = toTensor(ctx);
    WeightMap          wm32 = buildWeightTensors(m);
    Tensor            *base = fullLogits(tf, buildHidden(tf, in, wm32), wm32);
    std::vector<float> baseProbs(base->data(), base->data() + base->getSize());
    softmaxRows(baseProbs.data(), 16, kVocab);

    const struct {
        DType dt;
        int   minTop1;
    } cases[] = {
        {DType::Fp16, 16}, {DType::Int8, 15},    {DType::Fp8E4M3, 13},
        {DType::Int4, 10}, {DType::Fp4E2M1, 14},  // block scale = maxAbs/6 uses the full e2m1 range
    };
    for (const auto &c : cases) {
        WeightMap          wq     = buildQuantWeightTensors(tf, m, c.dt, 64);
        Tensor            *logits = fullLogits(tf, buildHidden(tf, in, wq), wq);
        std::vector<float> probs(logits->data(), logits->data() + logits->getSize());
        softmaxRows(probs.data(), 16, kVocab);

        float maxDiff = 0.f;
        int   top1    = 0;
        for (int r = 0; r < 16; ++r) {
            int a = 0, b = 0;
            for (int c2 = 0; c2 < kVocab; ++c2) {
                const float e = probs[static_cast<size_t>(r) * kVocab + c2];
                const float g = baseProbs[static_cast<size_t>(r) * kVocab + c2];
                maxDiff       = std::max(maxDiff, std::fabs(e - g));
                if (e > probs[static_cast<size_t>(r) * kVocab + a]) a = c2;
                if (g > baseProbs[static_cast<size_t>(r) * kVocab + b]) b = c2;
            }
            if (a == b) ++top1;
        }
        long long bytes = 0;
        for (const auto &kv : m.weights) {
            const int          n      = int(kv.second.data.size());
            const std::string &nm     = kv.first;
            const bool         matrix = nm.find(".bias") == std::string::npos && nm.find("ln_") == std::string::npos;
            if (matrix) {
                bytes += q::quantByteSize(c.dt, n);
                if (c.dt == DType::Int8 || c.dt == DType::Int4)
                    bytes += int64_t((n + 63) / 64) * int64_t(sizeof(float));
            } else {
                bytes += int64_t(n) * int64_t(sizeof(float));
            }
        }
        std::fprintf(stderr,
                     "[llm] quant %s: max prob diff = %.4f, top-1 %d/16, weights %.2f MB "
                     "(fp32 27.6 MB)\n",
                     quantTag(c.dt), maxDiff, top1, double(bytes) / (1024.0 * 1024.0));
        CHECK_GE(top1, c.minTop1);
        delete logits;
        for (auto &kv : wq) delete kv.second;
    }
    delete base;
    delete in;
}

// tensor_onnx.cpp
namespace {
using Bytes = std::vector<uint8_t>;
[[maybe_unused]] void varint(Bytes &b, uint64_t n) {
    while (n >= 128) {
        b.push_back(static_cast<uint8_t>(n) | 128);
        n >>= 7;
    }
    b.push_back(static_cast<uint8_t>(n));
}
[[maybe_unused]] void number(Bytes &b, int id, uint64_t n) {
    varint(b, id * 8);
    varint(b, n);
}
[[maybe_unused]] void field(Bytes &b, int id, const Bytes &value) {
    varint(b, id * 8 + 2);
    varint(b, value.size());
    b.insert(b.end(), value.begin(), value.end());
}
[[maybe_unused]] Bytes text(const char *s) { return Bytes(s, s + std::char_traits<char>::length(s)); }
[[maybe_unused]] Bytes unknownModel() {
    Bytes node, graph, output, opset, model;
    field(node, 2, text("y"));
    field(node, 3, text("unsupported-node"));
    field(node, 4, text("UnknownOperator"));
    field(graph, 1, node);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    return model;
}
}  // namespace


TEST_CASE("tensor.onnx.import.transactionAndDiagnostics") {
    using eve::tensor::OnnxModel;
    auto bytes = unknownModel();
    auto good  = OnnxModel::load(bytes);
    REQUIRE(good.ok());
    CHECK(good.value()->info().unsupportedNodes.size() == 1);
    auto rejected = good.value()->run({});
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK(rejected.error()->code() == eve::DiagnosticCode::Unsupported);
    CHECK(rejected.error()->path() == "unsupported-node");
    bytes.pop_back();
    auto broken = OnnxModel::load(bytes);
    CHECK(!broken.ok());
    CHECK(good.value()->info().nodeCount == 1);
    auto empty = OnnxModel::load({});
    CHECK(!empty.ok());
    auto overflow = OnnxModel::load(Bytes(12, 255));
    CHECK(!overflow.ok());
}


TEST_CASE("tensor.onnx.gpu.failureAndScope") {
    class FailedGpu final : public eve::tensor::OnnxCompute {
    public:
        int                               begins = 0, ends = 0, dispatches = 0;
        eve::Result<std::vector<uint8_t>> dispatch(const eve::tensor::OnnxKernel &) override {
            ++dispatches;
            return eve::Result<std::vector<uint8_t>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected GPU failure"));
        }

    protected:
        void beginRun() noexcept override { ++begins; }
        void endRun() noexcept override { ++ends; }
    } gpu;

    auto unsupported = eve::tensor::OnnxModel::load(unknownModel());
    REQUIRE(unsupported.ok());

    auto rejected = unsupported.value()->runGpu({}, gpu);

    CHECK(!rejected.ok());
    CHECK(gpu.dispatches == 0);
    CHECK(gpu.begins == 1);
    CHECK(gpu.ends == 1);
    Bytes tensor, node, graph, output, opset, model;
    number(tensor, 2, 1);
    field(tensor, 8, text("x"));
    float value = -2;
    Bytes data(4);
    std::memcpy(data.data(), &value, 4);
    field(tensor, 9, data);
    field(node, 1, text("x"));
    field(node, 2, text("y"));
    field(node, 3, text("relu-test"));
    field(node, 4, text("Relu"));
    field(graph, 1, node);
    field(graph, 5, tensor);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    auto imported = eve::tensor::OnnxModel::load(model);
    REQUIRE(imported.ok());

    auto failed = imported.value()->runGpu({}, gpu);
    REQUIRE(!failed.ok());
    CHECK(failed.error()->code() == eve::DiagnosticCode::Failed);
    CHECK(failed.error()->path() == "relu-test");
    CHECK(gpu.dispatches == 1);
    CHECK(gpu.begins == 2);
    CHECK(gpu.ends == 2);
    auto cpu = imported.value()->run({});
    REQUIRE(cpu.ok());
    float out = -1;
    std::memcpy(&out, cpu.value()[0].tensor.bytes.data(), 4);
    CHECK(out == 0);
}


// tensor_optimizer.cpp
using namespace eve::tensor;


// tensor_quant.cpp
using namespace eve::tensor;

namespace {

[[maybe_unused]] std::vector<float> randData(int n, unsigned seed) {
    std::vector<float> v(static_cast<size_t>(n));
    unsigned           s = seed;
    for (int i = 0; i < n; ++i) {
        s                         = s * 1664525u + 1013904223u;
        v[static_cast<size_t>(i)] = float(int(s % 2001) - 1000) / 1000.f;  // [-1, 1]
    }
    return v;
}

[[maybe_unused]] Tensor *fromData(const std::vector<float> &d, int d0, int d1) {
    auto *t = new Tensor(d0, d1);
    for (size_t i = 0; i < d.size(); ++i) t->set(int(i), d[static_cast<size_t>(i)]);
    return t;
}

[[maybe_unused]] float maxAbsDiff(const Tensor *a, const Tensor *b) {
    float m = 0.f;
    for (int i = 0; i < a->getSize(); ++i) m = std::max(m, std::fabs(a->get(i) - b->get(i)));
    return m;
}

[[maybe_unused]] const char *dtypeTag(DType dt) {
    switch (dt) {
        case DType::Fp16: return "fp16";
        case DType::Fp8E4M3: return "fp8";
        case DType::Fp4E2M1: return "fp4";
        case DType::Int8: return "int8";
        case DType::Int4: return "int4";
        default: return "?";
    }
}

}  // namespace
}  // namespace test_tensor_llm
#endif
