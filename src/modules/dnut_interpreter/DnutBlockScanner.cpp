#include "dnut_interpreter/DnutBlockScanner.h"

#include <string>
#include <utility>
#include <vector>

namespace eve::dnut {
namespace {

eve::Result<std::vector<DnutBlock>> scanFailure(const std::string& path, const DnutToken& token, std::string message) {
    return eve::Result<std::vector<DnutBlock>>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::ParseError, message, path,
        {{"line", std::to_string(token.line)}, {"column", std::to_string(token.column)}}, "dnut.block-scanner"));
}

bool isPunct(const DnutToken& token, const char* text) {
    return token.kind == DnutTokenKind::Punctuator && token.text == text;
}

}  // namespace

eve::Result<std::vector<DnutBlock>> scanDnutBlocks(const std::vector<DnutToken>& tokens, const std::string& path) {
    std::vector<DnutBlock> blocks;
    if (tokens.empty())
        return eve::Result<std::vector<DnutBlock>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "dnut token buffer is empty", path, {}, "dnut.block-scanner"));

    std::size_t index = 0;
    while (index < tokens.size() && tokens[index].kind != DnutTokenKind::EndOfFile) {
        const DnutToken& keyword = tokens[index];
        if (keyword.kind != DnutTokenKind::Identifier)
            return scanFailure(path, keyword, "expected a top-level field or dialect block");

        // The versioned document envelope is not a dialect block.
        if (keyword.text == "schema") {
            if (index + 1 >= tokens.size() || tokens[index + 1].kind != DnutTokenKind::String)
                return scanFailure(path, keyword, "schema requires a quoted identifier");
            index += 2;
            continue;
        }
        if (keyword.text == "version") {
            if (index + 1 >= tokens.size() || tokens[index + 1].kind != DnutTokenKind::Number)
                return scanFailure(path, keyword, "version requires an integer");
            index += 2;
            continue;
        }

        DnutBlock block;
        block.kind       = keyword.text;
        block.beginToken = index;
        block.line       = keyword.line;
        block.column     = keyword.column;
        ++index;
        if (index < tokens.size() &&
            (tokens[index].kind == DnutTokenKind::Identifier || tokens[index].kind == DnutTokenKind::String))
            block.id = tokens[index].text;

        while (index < tokens.size() && tokens[index].kind != DnutTokenKind::EndOfFile && !isPunct(tokens[index], "{"))
            ++index;
        if (index >= tokens.size() || tokens[index].kind == DnutTokenKind::EndOfFile)
            return scanFailure(path, keyword, "top-level block '" + block.kind + "' requires '{'");

        int depth = 0;
        while (index < tokens.size() && tokens[index].kind != DnutTokenKind::EndOfFile) {
            if (isPunct(tokens[index], "{"))
                ++depth;
            else if (isPunct(tokens[index], "}")) {
                --depth;
                if (depth == 0) {
                    ++index;
                    block.endToken = index;
                    blocks.push_back(std::move(block));
                    break;
                }
            }
            ++index;
        }
        if (block.endToken == 0) return scanFailure(path, keyword, "unterminated top-level block '" + block.kind + "'");
    }
    return eve::Result<std::vector<DnutBlock>>::success(std::move(blocks));
}

}  // namespace eve::dnut
