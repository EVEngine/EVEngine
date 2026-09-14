#include "devtools/LanguageText.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace eve::dev::lsp {
namespace {

enum class ScanKind { Code, LineComment, BlockComment, String };

struct ScanState {
    ScanKind kind      = ScanKind::Code;
    char     quote     = 0;
    bool     verbatim  = false;
};

void advance(ScanState& state, std::string_view source, size_t& i, size_t end) {
    if (i >= end) return;
    const char value = source[i];
    const char next  = i + 1 < end ? source[i + 1] : 0;
    if (state.kind == ScanKind::LineComment) {
        if (value == '\n') state.kind = ScanKind::Code;
        ++i;
        return;
    }
    if (state.kind == ScanKind::BlockComment) {
        if (value == '*' && next == '/') {
            state.kind = ScanKind::Code;
            i += 2;
            return;
        }
        ++i;
        return;
    }
    if (state.kind == ScanKind::String) {
        if (!state.verbatim && value == '\\' && i + 1 < end) {
            i += 2;
            return;
        }
        if (state.verbatim && value == state.quote && next == state.quote) {
            i += 2;
            return;
        }
        if (value == state.quote) {
            state.kind = ScanKind::Code;
            ++i;
            return;
        }
        ++i;
        return;
    }
    if (value == '/' && next == '/') {
        state.kind = ScanKind::LineComment;
        i += 2;
        return;
    }
    if (value == '/' && next == '*') {
        state.kind = ScanKind::BlockComment;
        i += 2;
        return;
    }
    const bool verbatim = value == '@' && (next == '"' || next == '\'');
    if (value == '"' || value == '\'' || verbatim) {
        state.kind     = ScanKind::String;
        state.quote    = verbatim ? next : value;
        state.verbatim = verbatim;
        i += verbatim ? 2 : 1;
        return;
    }
    ++i;
}

std::string indentText(unsigned depth, const FormatOptions& options) {
    if (depth == 0) return {};
    if (options.insertSpaces) return std::string(static_cast<size_t>(depth) * options.tabSize, ' ');
    return std::string(depth, '\t');
}

std::string rtrim(std::string_view line) {
    size_t end = line.size();
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) --end;
    return std::string(line.substr(0, end));
}

unsigned leadingCloses(std::string_view line, ScanState state) {
    unsigned closes = 0;
    size_t   i      = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    while (i < line.size()) {
        const size_t before = i;
        const auto   kind   = state.kind;
        advance(state, line, i, line.size());
        if (kind != ScanKind::Code) continue;
        if (before >= line.size()) break;
        const char value = line[before];
        if (value == '}' || value == ']') ++closes;
        else if (value == '{' || value == '[')
            break;
        else if (!std::isspace(static_cast<unsigned char>(value)))
            break;
        if (i == before) ++i;
    }
    return closes;
}

int braceDelta(std::string_view line, ScanState& state) {
    int    delta = 0;
    size_t i     = 0;
    while (i < line.size()) {
        const size_t before = i;
        const auto   kind   = state.kind;
        advance(state, line, i, line.size());
        if (kind != ScanKind::Code) continue;
        if (before >= line.size()) break;
        const char value = line[before];
        if (value == '{' || value == '[') ++delta;
        else if (value == '}' || value == ']')
            --delta;
        if (i == before) ++i;
    }
    return delta;
}

Position positionAtOffset(std::string_view source, size_t offset) {
    Position position;
    for (size_t i = 0; i < offset && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++position.line;
            position.character = 0;
        } else {
            ++position.character;
        }
    }
    return position;
}

}  // namespace

size_t offsetAt(std::string_view source, Position position) noexcept {
    size_t offset = 0;
    for (size_t line = 0; line < position.line && offset < source.size(); ++line) {
        const size_t newline = source.find('\n', offset);
        if (newline == std::string_view::npos) return source.size();
        offset = newline + 1;
    }
    return std::min(offset + position.character, source.size());
}

Range documentRange(std::string_view source) noexcept {
    return {{0, 0}, positionAtOffset(source, source.size())};
}

size_t lastLineIndex(std::string_view source) noexcept {
    size_t line = 0;
    for (const char value : source)
        if (value == '\n') ++line;
    return line;
}

Range coveringLines(std::string_view source, size_t startLine, size_t endLineInclusive) noexcept {
    const size_t last = lastLineIndex(source);
    if (startLine > last) startLine = last;
    if (endLineInclusive > last) endLineInclusive = last;
    if (endLineInclusive < startLine) endLineInclusive = startLine;
    Range range;
    range.start = {startLine, 0};
    if (endLineInclusive < last)
        range.end = {endLineInclusive + 1, 0};
    else
        range.end = positionAtOffset(source, source.size());
    return range;
}

std::string sliceLines(std::string_view source, size_t startLine, size_t endLineInclusive) {
    const Range range = coveringLines(source, startLine, endLineInclusive);
    const size_t begin = offsetAt(source, range.start);
    const size_t end   = offsetAt(source, range.end);
    if (begin > end) return {};
    return std::string(source.substr(begin, end - begin));
}

bool applyIncremental(std::string& source, const Range* range, std::string_view text) {
    if (range == nullptr) {
        source = std::string(text);
        return true;
    }
    const size_t begin = offsetAt(source, range->start);
    const size_t end   = offsetAt(source, range->end);
    if (begin > end || end > source.size()) return false;
    source.replace(begin, end - begin, text);
    return true;
}

std::string formatEveScript(std::string_view source, const FormatOptions& options) {
    std::string result;
    result.reserve(source.size());
    ScanState state;
    int       indent = 0;
    size_t    lineBegin = 0;
    const unsigned tab = options.tabSize == 0 ? 4 : options.tabSize;
    FormatOptions  normalized = options;
    normalized.tabSize        = tab;
    for (size_t i = 0; i <= source.size(); ++i) {
        if (i < source.size() && source[i] != '\n') continue;
        const std::string_view raw = source.substr(lineBegin, i - lineBegin);
        const std::string      trimmed = rtrim(raw);
        size_t                 content = 0;
        while (content < trimmed.size() && (trimmed[content] == ' ' || trimmed[content] == '\t')) ++content;
        const bool blank = content == trimmed.size();
        if (blank) {
            if (state.kind == ScanKind::BlockComment) result += trimmed;
        } else if (state.kind == ScanKind::BlockComment) {
            result += trimmed;
        } else {
            const unsigned closes = leadingCloses(trimmed, state);
            const int      display = indent - static_cast<int>(closes);
            result += indentText(display < 0 ? 0 : static_cast<unsigned>(display), normalized);
            result += trimmed.substr(content);
        }
        indent += braceDelta(trimmed, state);
        if (indent < 0) indent = 0;
        if (i < source.size()) result += '\n';
        lineBegin = i + 1;
    }
    return result;
}

std::vector<FoldingRange> foldingRanges(std::string_view source) {
    struct Open {
        size_t   line = 0;
        char     kind = '{';
        ScanKind scan = ScanKind::Code;
    };
    std::vector<Open>         stack;
    std::vector<FoldingRange> result;
    ScanState                 state;
    size_t                    line = 0;
    for (size_t i = 0; i < source.size();) {
        const size_t before = i;
        const auto   kind   = state.kind;
        const char   value  = source[i];
        advance(state, source, i, source.size());
        if (value == '\n') {
            ++line;
            continue;
        }
        if (kind == ScanKind::Code && state.kind == ScanKind::BlockComment) {
            stack.push_back({line, '*', ScanKind::BlockComment});
            continue;
        }
        if (kind == ScanKind::BlockComment && state.kind == ScanKind::Code) {
            if (!stack.empty() && stack.back().kind == '*') {
                const size_t start = stack.back().line;
                stack.pop_back();
                if (line > start) result.push_back({start, line, "comment"});
            }
            continue;
        }
        if (kind != ScanKind::Code) continue;
        if (value == '{' || value == '[') stack.push_back({line, value, ScanKind::Code});
        else if ((value == '}' || value == ']') && !stack.empty() && stack.back().kind != '*') {
            const size_t start = stack.back().line;
            stack.pop_back();
            if (line > start) result.push_back({start, line, "region"});
        }
        if (i == before) ++i;
    }
    return result;
}

}  // namespace eve::dev::lsp
