#include "dnut_interpreter/DnutLexer.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace eve::dnut {

namespace {

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierBody(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

eve::Result<std::vector<DnutToken>> lexFailure(const std::string& path, int line, int column,
                                               std::string message) {
    return eve::Result<std::vector<DnutToken>>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message), path,
                               {{"line", std::to_string(line)}, {"column", std::to_string(column)}},
                               "dnut.lexer"));
}

/** @brief Number of characters consumed by a two-character punctuator at `i`, or zero. */
std::size_t twoCharacterPunctuatorLength(std::string_view source, std::size_t i) {
    if (i + 1 >= source.size()) return 0;
    const std::string_view pair = source.substr(i, 2);
    for (const std::string_view candidate : {"==", "!=", ">=", "<=", "&&", "||", "->"}) {
        if (pair == candidate) return 2;
    }
    return 0;
}

bool isSingleCharacterPunctuator(char c) {
    switch (c) {
        case '{':
        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case ':':
        case ',':
        case '=':
        case '>':
        case '<':
        case '!':
        case '-':
        case '+':
        case '*':
        case '/':
        case '|': return true;
        default: return false;
    }
}

/** @brief Append the escape target of `escape` to `out`; returns false for an unknown escape. */
bool appendEscape(char escape, std::string& out) {
    switch (escape) {
        case 'n': out += '\n'; return true;
        case 't': out += '\t'; return true;
        case 'r': out += '\r'; return true;
        case '"': out += '"'; return true;
        case '\'': out += '\''; return true;
        case '\\': out += '\\'; return true;
        case '{': out += '{'; return true;
        case '}': out += '}'; return true;
        default: return false;
    }
}

}  // namespace

const char* dnutTokenKindName(DnutTokenKind kind) noexcept {
    switch (kind) {
        case DnutTokenKind::Identifier: return "identifier";
        case DnutTokenKind::String: return "string";
        case DnutTokenKind::Number: return "number";
        case DnutTokenKind::Punctuator: return "punctuator";
        case DnutTokenKind::EndOfFile: return "eof";
    }
    return "unknown";
}

eve::Result<std::vector<DnutToken>> lexDnut(std::string_view source, const std::string& path) {
    std::vector<DnutToken> tokens;
    std::size_t            i    = 0;
    int                    line = 1;
    int                    column = 1;
    const std::size_t      size = source.size();

    const auto advanceOne = [&]() {
        if (source[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
        ++i;
    };

    while (i < size) {
        const char c = source[i];

        if (c == '\n' || c == ' ' || c == '\t' || c == '\r') {
            advanceOne();
            continue;
        }
        if (c == '/' && i + 1 < size && source[i + 1] == '/') {
            while (i < size && source[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < size && source[i + 1] == '*') {
            const int startLine   = line;
            const int startColumn = column;
            advanceOne();
            advanceOne();
            bool closed = false;
            while (i < size) {
                if (source[i] == '*' && i + 1 < size && source[i + 1] == '/') {
                    advanceOne();
                    advanceOne();
                    closed = true;
                    break;
                }
                advanceOne();
            }
            if (!closed)
                return lexFailure(path, startLine, startColumn, "unterminated block comment");
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote       = c;
            const int  startLine   = line;
            const int  startColumn = column;
            advanceOne();
            std::string text;
            bool        terminated = false;
            while (i < size) {
                if (source[i] == quote) {
                    advanceOne();
                    terminated = true;
                    break;
                }
                if (source[i] == '\\') {
                    advanceOne();
                    if (i >= size) break;
                    if (!appendEscape(source[i], text))
                        return lexFailure(path, line, column,
                                          std::string("unknown escape sequence '\\") + source[i] + "'");
                    advanceOne();
                    continue;
                }
                text += source[i];
                advanceOne();
            }
            if (!terminated) return lexFailure(path, startLine, startColumn, "unterminated string literal");
            tokens.push_back({DnutTokenKind::String, std::move(text), startLine, startColumn});
            continue;
        }
        if (isDigit(c)) {
            const int    startLine   = line;
            const int    startColumn = column;
            const std::size_t start  = i;
            while (i < size) {
                const char d = source[i];
                if (isDigit(d) || d == '.') {
                    advanceOne();
                } else if ((d == 'e' || d == 'E') && i + 1 < size &&
                           (isDigit(source[i + 1]) || source[i + 1] == '+' || source[i + 1] == '-')) {
                    advanceOne();
                    advanceOne();
                } else {
                    break;
                }
            }
            tokens.push_back({DnutTokenKind::Number, std::string(source.substr(start, i - start)), startLine,
                              startColumn});
            continue;
        }
        if (isIdentifierStart(c)) {
            const int    startLine   = line;
            const int    startColumn = column;
            const std::size_t start  = i;
            // A '-' joins an identifier only when a word character follows it, so
            // dashed content ids (`iron-sword`) stay one token while arrow and
            // minus punctuation (`->`, `-3`) are unaffected.
            const auto continuesIdentifier = [&source, size](std::size_t position) {
                if (isIdentifierBody(source[position])) return true;
                return source[position] == '-' && position + 1 < size && isIdentifierBody(source[position + 1]);
            };
            while (i < size && continuesIdentifier(i)) advanceOne();
            tokens.push_back(
                {DnutTokenKind::Identifier, std::string(source.substr(start, i - start)), startLine, startColumn});
            continue;
        }
        if (const std::size_t pairLength = twoCharacterPunctuatorLength(source, i); pairLength != 0) {
            const int startLine   = line;
            const int startColumn = column;
            const std::string text(source.substr(i, pairLength));
            advanceOne();
            advanceOne();
            tokens.push_back({DnutTokenKind::Punctuator, text, startLine, startColumn});
            continue;
        }
        if (isSingleCharacterPunctuator(c)) {
            const int startLine   = line;
            const int startColumn = column;
            const std::string text(1, c);
            advanceOne();
            tokens.push_back({DnutTokenKind::Punctuator, std::move(text), startLine, startColumn});
            continue;
        }
        return lexFailure(path, line, column, std::string("unexpected character '") + c + "'");
    }

    tokens.push_back({DnutTokenKind::EndOfFile, std::string{}, line, column});
    return eve::Result<std::vector<DnutToken>>::success(std::move(tokens));
}

bool hasErrors(const std::vector<DnutDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DnutSeverity::Error) return true;
    }
    return false;
}

}  // namespace eve::dnut
