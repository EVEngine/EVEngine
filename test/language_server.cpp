#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/LanguageServer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using eve::dev::LanguageServer;
using eve::dev::LspDispatch;
using eve::dev::lsp::Position;

namespace {

std::filesystem::path tempProject(const char* tag) {
    const auto          path = std::filesystem::temp_directory_path() / tag;
    std::error_code     error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path / "scripts", error);
    return path;
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool hasLabel(const std::vector<eve::script::ScriptCompletion>& items, std::string_view label) {
    for (const auto& item : items)
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
    for (const auto& symbol : symbols)
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
    CHECK_EQ(server.handleMessage(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", output),
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
    CHECK_EQ(server.handleMessage(open, output), LspDispatch::Continue);
    CHECK(output.str().find("publishDiagnostics") != std::string::npos);
    CHECK(output.str().find("EVE2601") != std::string::npos);

    output.str("");
    output.clear();
    CHECK_EQ(server.handleMessage(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})", output), LspDispatch::Continue);
    CHECK_EQ(server.handleMessage(R"({"jsonrpc":"2.0","method":"exit"})", output), LspDispatch::Exit);

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
    CHECK_EQ(server.handleMessage(change, output), LspDispatch::Continue);
    const auto afterIncremental = server.hover(uri, Position{1, 6});
    REQUIRE(afterIncremental.has_value());
    CHECK(afterIncremental->markdown.find("n") != std::string::npos);

    output.str("");
    output.clear();
    CHECK_EQ(server.handleMessage(
                 std::string(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/formatting","params":{"textDocument":{"uri":")") +
                     uri + R"(},"options":{"tabSize":4,"insertSpaces":true}}})",
                 output),
             LspDispatch::Continue);
    CHECK(output.str().find("local n = 1") != std::string::npos);

    output.str("");
    output.clear();
    CHECK_EQ(server.handleMessage(
                 std::string(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/foldingRange","params":{"textDocument":{"uri":")") +
                     uri + R"("}}})",
                 output),
             LspDispatch::Continue);
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
        for (const auto& token : tokens)
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
