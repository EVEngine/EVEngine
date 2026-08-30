#include "cmdline/LanguageIndex.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using eve::cmd::lsp::Position;
using eve::cmd::lsp::WorkspaceIndex;

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
