#include "devtools/LanguageIndex.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>

using eve::dev::lsp::Position;
using eve::dev::lsp::WorkspaceIndex;

TEST_CASE("languageIndex.crossFileDefinitionAndReferences") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut",
                 "export function greet(name: string) -> string { return name }\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { greet } from \"./lib.nut\"\n"
                 "local message = greet(\"Ada\")\n");

    const auto definition = index.definition("game:/main.nut", Position{1, 17});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));
    CHECK_EQ(definition->range.start.line, size_t(0));
    CHECK_EQ(definition->range.start.character, size_t(16));

    const auto references = index.references("game:/lib.nut", Position{0, 17}, true);
    CHECK_EQ(references.size(), size_t(3));
    const auto usages = index.references("game:/lib.nut", Position{0, 17}, false);
    CHECK_EQ(usages.size(), size_t(2));
}

TEST_CASE("languageIndex.renameExportUpdatesImportAndUsages") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export const SPEED = 2.0\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { SPEED } from \"./lib.nut\"\nlocal value = SPEED\n");

    const auto edits = index.rename("game:/lib.nut", Position{0, 14}, "PLAYER_SPEED");
    REQUIRE(edits.has_value());
    CHECK_EQ(edits->size(), size_t(3));
    for (const auto& edit : *edits) CHECK_EQ(edit.newText, std::string("PLAYER_SPEED"));
}

TEST_CASE("languageIndex.renameImportAliasStaysLocal") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export function greet() {}\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { greet as salute } from \"./lib.nut\"\nsalute()\n");

    const auto definition = index.definition("game:/main.nut", Position{1, 2});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));

    const auto edits = index.rename("game:/main.nut", Position{1, 2}, "hello");
    REQUIRE(edits.has_value());
    CHECK_EQ(edits->size(), size_t(2));
    for (const auto& edit : *edits) CHECK_EQ(edit.location.uri, std::string("file:///game/main.nut"));
}

TEST_CASE("languageIndex.namespaceImportSupportsDefinitionReferencesAndRename") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export function greet() {}\n");
    index.update("game:/namespace.nut", "file:///game/namespace.nut",
                 "import * as api from \"./lib.nut\"\napi.greet()\n");
    index.update("game:/named.nut", "file:///game/named.nut", "import { greet } from \"./lib.nut\"\ngreet()\n");

    const auto definition = index.definition("game:/namespace.nut", Position{1, 6});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));

    const auto references = index.references("game:/namespace.nut", Position{1, 6}, true);
    CHECK_EQ(references.size(), size_t(4));

    const auto edits = index.rename("game:/namespace.nut", Position{1, 6}, "welcome");
    REQUIRE(edits.has_value());
    CHECK_EQ(edits->size(), size_t(4));
    for (const auto& edit : *edits) CHECK_EQ(edit.newText, std::string("welcome"));
}

TEST_CASE("languageIndex.rejectsInvalidRename") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut", "local value = 1\n");
    CHECK(!index.rename("game:/main.nut", Position{0, 7}, "not-valid").has_value());
}

TEST_CASE("languageIndex.semanticTokensDistinguishClassFunctionVariable") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut",
                 "class Player {}\n"
                 "function greet(name) { return name }\n"
                 "local speed = 1\n"
                 "const MAX = 2\n"
                 "greet(\"x\")\n"
                 "Player()\n"
                 "speed = MAX\n");

    const auto tokens = index.semanticTokens("game:/main.nut");
    const auto typeAt = [&](std::string_view name, size_t line) -> uint32_t {
        for (const auto& token : tokens)
            if (token.name == name && token.start.line == line) return token.type;
        return 99;
    };
    CHECK_EQ(typeAt("Player", 0), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("greet", 1), eve::dev::lsp::SemanticTypes::Function);
    CHECK_EQ(typeAt("speed", 2), eve::dev::lsp::SemanticTypes::Variable);
    CHECK_EQ(typeAt("MAX", 3), eve::dev::lsp::SemanticTypes::Variable);
    CHECK_EQ(typeAt("greet", 4), eve::dev::lsp::SemanticTypes::Function);
    CHECK_EQ(typeAt("Player", 5), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("speed", 6), eve::dev::lsp::SemanticTypes::Variable);
}

TEST_CASE("languageIndex.semanticTokensLiteralsAndSelf") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut", "local ready = true\nthis.base = null\n");
    const auto tokens = index.semanticTokens("game:/main.nut");
    const auto typeAt = [&](std::string_view name, size_t line) -> uint32_t {
        for (const auto& token : tokens)
            if (token.name == name && token.start.line == line) return token.type;
        return 99;
    };
    CHECK_EQ(typeAt("true", 0), eve::dev::lsp::SemanticTypes::Keyword);
    CHECK_EQ(typeAt("this", 1), eve::dev::lsp::SemanticTypes::Keyword);
    CHECK_EQ(typeAt("base", 1), eve::dev::lsp::SemanticTypes::Keyword);
    CHECK_EQ(typeAt("null", 1), eve::dev::lsp::SemanticTypes::Keyword);
}
