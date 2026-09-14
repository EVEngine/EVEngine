#include "devtools/LanguageText.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using eve::dev::lsp::FormatOptions;
using eve::dev::lsp::Range;

TEST_CASE("devtools.languageText.incrementalFormatAndFold") {
    std::string source = "function foo() {\nlocal x = 1\n}\n";
    Range       insert{{1, 6}, {1, 7}};
    CHECK(eve::dev::lsp::applyIncremental(source, &insert, "value"));
    CHECK_EQ(source, std::string("function foo() {\nlocal value = 1\n}\n"));

    const std::string formatted = eve::dev::lsp::formatEveScript(source);
    CHECK_EQ(formatted, std::string("function foo() {\n    local value = 1\n}\n"));

    FormatOptions tabs;
    tabs.insertSpaces = false;
    tabs.tabSize      = 4;
    CHECK_EQ(eve::dev::lsp::formatEveScript("{\na\n}\n", tabs), std::string("{\n\ta\n}\n"));

    const auto folds = eve::dev::lsp::foldingRanges("function foo() {\n    local x = 1\n}\n/*\nblock\n*/\n");
    bool       foundBrace   = false;
    bool       foundComment = false;
    for (const auto& fold : folds) {
        if (fold.kind == "region" && fold.startLine == 0 && fold.endLine == 2) foundBrace = true;
        if (fold.kind == "comment" && fold.startLine == 3 && fold.endLine == 5) foundComment = true;
    }
    CHECK(foundBrace);
    CHECK(foundComment);

    CHECK_EQ(eve::dev::lsp::sliceLines("a\nb\nc\n", 1, 1), std::string("b\n"));
    const Range span = eve::dev::lsp::coveringLines("a\nb\nc\n", 1, 1);
    CHECK_EQ(span.start.line, size_t(1));
    CHECK_EQ(span.end.line, size_t(2));
    CHECK_EQ(span.end.character, size_t(0));
}
