#include "dialogue/DnutParser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

enum class Tok { Ident, Str, Num, Punct, Eof };

struct Token {
    Tok kind = Tok::Eof;
    std::string value;
    int line = 1;
};

std::string floatToString(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string scalarToString(const DataValue &v) {
    switch (v.kind) {
        case DataValue::Kind::String:
            return v.s;
        case DataValue::Kind::Int:
            return std::to_string(v.i);
        case DataValue::Kind::Float:
            return floatToString(v.f);
        case DataValue::Kind::Bool:
            return v.b ? "true" : "false";
        default:
            return {};
    }
}

DataValue numValue(const std::string &raw) {
    if (raw.find_first_of(".eE") != std::string::npos)
        return DataValue::number(std::strtod(raw.c_str(), nullptr));
    return DataValue::integer(std::strtoll(raw.c_str(), nullptr, 10));
}

DataValue negNumValue(const std::string &raw) {
    DataValue v = numValue(raw);
    if (v.kind == DataValue::Kind::Int) v.i = -v.i;
    else v.f = -v.f;
    return v;
}

class Parser {
public:
    Parser(std::string source, std::string path) : source_(std::move(source)), path_(std::move(path)) {}

    bool parse(DataValue &out, std::string &error) {
        try {
            tokenize();
            out = parsePools();
            return true;
        } catch (const ParseError &e) {
            error = e.what();
            return false;
        }
    }

private:
    struct ParseError : std::runtime_error {
        explicit ParseError(const std::string &msg) : std::runtime_error(msg) {}
    };

    std::string source_;
    std::string path_;
    std::vector<Token> toks_;
    size_t pos_ = 0;

    const Token &cur() const { return toks_[pos_]; }
    Token adv() {
        Token t = toks_[pos_];
        if (pos_ + 1 < toks_.size()) ++pos_;
        return t;
    }
    bool isPunct(const std::string &p) const {
        return cur().kind == Tok::Punct && cur().value == p;
    }
    bool isIdent() const { return cur().kind == Tok::Ident; }

    [[noreturn]] void fail(const std::string &msg) const {
        const std::string at = cur().kind == Tok::Eof ? "<结束>" : cur().value;
        throw ParseError(path_ + ":" + std::to_string(cur().line) + ": " + msg +
                         "（实际是 '" + at + "'）");
    }
    void expectPunct(const std::string &p) {
        if (!isPunct(p)) fail("期望 '" + p + "'");
        adv();
    }
    std::string expectIdent(const std::string &what) {
        if (!isIdent()) fail("期望标识符 " + what);
        return adv().value;
    }

    void tokenize() {
        toks_.clear();
        const std::string &src = source_;
        const size_t n = src.size();
        size_t i = 0;
        int line = 1;
        const auto add = [&](Tok k, std::string v) {
            toks_.push_back(Token{k, std::move(v), line});
        };

        while (i < n) {
            const char c = src[i];
            if (c == '\n') {
                ++line;
                ++i;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && src[i + 1] == '/') {
                while (i < n && src[i] != '\n') ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && src[i + 1] == '*') {
                const int startLine = line;
                i += 2;
                while (i < n && !(src[i] == '*' && i + 1 < n && src[i + 1] == '/')) {
                    if (src[i] == '\n') ++line;
                    ++i;
                }
                if (i >= n)
                    throw ParseError(path_ + ":" + std::to_string(startLine) + ": 未闭合的块注释");
                i += 2;
                continue;
            }
            if (c == '"' || c == '\'') {
                const char quote = c;
                const int startLine = line;
                ++i;
                std::string s;
                while (i < n && src[i] != quote) {
                    if (src[i] == '\\' && i + 1 < n) {
                        ++i;
                        const char e = src[i];
                        switch (e) {
                            case 'n': s += '\n'; break;
                            case 't': s += '\t'; break;
                            case 'r': s += '\r'; break;
                            case '"': s += '"'; break;
                            case '\'': s += '\''; break;
                            case '\\': s += '\\'; break;
                            case '{': s += '{'; break;
                            case '}': s += '}'; break;
                            default: s += e; break;
                        }
                        ++i;
                    } else {
                        s += src[i++];
                    }
                }
                if (i >= n)
                    throw ParseError(path_ + ":" + std::to_string(startLine) + ": 未闭合的字符串");
                ++i;
                add(Tok::Str, std::move(s));
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
                const size_t start = i;
                while (i < n) {
                    const char d = src[i];
                    if (d >= '0' && d <= '9') {
                        ++i;
                    } else if (d == '.') {
                        ++i;
                    } else if ((d == 'e' || d == 'E') && i + 1 < n &&
                               ((src[i + 1] >= '0' && src[i + 1] <= '9') ||
                                src[i + 1] == '+' || src[i + 1] == '-')) {
                        i += 2;
                    } else {
                        break;
                    }
                }
                add(Tok::Num, src.substr(start, i - start));
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const size_t start = i;
                while (i < n) {
                    const char d = src[i];
                    if (std::isalnum(static_cast<unsigned char>(d)) || d == '_' || d == '.') ++i;
                    else break;
                }
                add(Tok::Ident, src.substr(start, i - start));
                continue;
            }
            const std::string two = (i + 1 < n) ? src.substr(i, 2) : "";
            if (two == "==" || two == "!=" || two == ">=" || two == "<=" ||
                two == "&&" || two == "||") {
                add(Tok::Punct, two);
                i += 2;
                continue;
            }
            if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']' ||
                c == ':' || c == ',' || c == '=' || c == '>' || c == '<' || c == '!' ||
                c == '-') {
                add(Tok::Punct, std::string(1, c));
                ++i;
                continue;
            }
            throw ParseError(path_ + ":" + std::to_string(line) + ": 无法识别的字符 '" +
                             std::string(1, c) + "'");
        }
        toks_.push_back(Token{Tok::Eof, "", line});
    }

    DataValue parseLiteralValue() {
        const Token &t = cur();
        if (t.kind == Tok::Str) {
            adv();
            return DataValue::string(t.value);
        }
        if (t.kind == Tok::Num) {
            adv();
            return numValue(t.value);
        }
        if (t.kind == Tok::Ident) {
            if (t.value == "true") {
                adv();
                return DataValue::boolean(true);
            }
            if (t.value == "false") {
                adv();
                return DataValue::boolean(false);
            }
            fail("条件字面量只支持字符串/数字/true/false");
        }
        if (isPunct("-")) {
            adv();
            const Token &tt = cur();
            if (tt.kind == Tok::Num) {
                adv();
                return negNumValue(tt.value);
            }
            fail("'-' 后应跟数字");
        }
        fail("期望字面量");
    }

    DataValue parseComparison() {
        if (!isIdent()) fail("条件左侧应为变量名");
        const std::string varName = adv().value;
        const Token &op = cur();
        if (op.kind != Tok::Punct ||
            (op.value != "==" && op.value != "!=" && op.value != ">" && op.value != "<" &&
             op.value != ">=" && op.value != "<="))
            fail("期望比较运算符（== != > < >= <=）");
        adv();
        DataValue value = parseLiteralValue();
        std::string mapped;
        if (op.value == "==") mapped = "eq";
        else if (op.value == "!=") mapped = "ne";
        else if (op.value == ">") mapped = "gt";
        else if (op.value == "<") mapped = "lt";
        else if (op.value == ">=") mapped = "ge";
        else mapped = "le";
        return DataValue::object({
            {"var", DataValue::string(varName)},
            {"op", DataValue::string(mapped)},
            {"value", std::move(value)},
        });
    }

    DataValue parseNot() {
        if (isPunct("!")) {
            adv();
            return DataValue::object({{"not", parseNot()}});
        }
        if (isPunct("(")) {
            adv();
            DataValue e = parseOr();
            expectPunct(")");
            return e;
        }
        return parseComparison();
    }

    DataValue parseAnd() {
        DataValue left = parseNot();
        while (isPunct("&&")) {
            adv();
            DataValue right = parseNot();
            bool appended = false;
            if (left.kind == DataValue::Kind::Object) {
                for (auto &kv : left.obj) {
                    if (kv.first == "all" && kv.second.kind == DataValue::Kind::Array) {
                        kv.second.arr.push_back(std::move(right));
                        appended = true;
                        break;
                    }
                }
            }
            if (!appended) {
                left = DataValue::object(
                    {{"all", DataValue::array({std::move(left), std::move(right)})}});
            }
        }
        return left;
    }

    DataValue parseOr() {
        DataValue left = parseAnd();
        while (isPunct("||")) {
            adv();
            DataValue right = parseAnd();
            bool appended = false;
            if (left.kind == DataValue::Kind::Object) {
                for (auto &kv : left.obj) {
                    if (kv.first == "any" && kv.second.kind == DataValue::Kind::Array) {
                        kv.second.arr.push_back(std::move(right));
                        appended = true;
                        break;
                    }
                }
            }
            if (!appended) {
                left = DataValue::object(
                    {{"any", DataValue::array({std::move(left), std::move(right)})}});
            }
        }
        return left;
    }

    void parseAttrs(const int lineNum, std::vector<std::pair<std::string, DataValue>> &out) {
        while (cur().line == lineNum && !isPunct("}") && cur().kind != Tok::Eof) {
            if (!isIdent()) fail("期望属性名");
            const std::string name = adv().value;
            if (name == "meta" && isPunct("(")) {
                adv();
                std::vector<std::pair<std::string, DataValue>> metaFields;
                while (!isPunct(")")) {
                    if (!isIdent()) fail("meta 键应为标识符");
                    const std::string k = adv().value;
                    expectPunct("=");
                    DataValue v = parseLiteralValue();
                    if (v.kind != DataValue::Kind::String && v.kind != DataValue::Kind::Int &&
                        v.kind != DataValue::Kind::Float && v.kind != DataValue::Kind::Bool)
                        fail("meta 值只支持标量");
                    metaFields.emplace_back(k, DataValue::string(scalarToString(v)));
                    if (isPunct(",")) adv();
                    else if (!isPunct(")")) fail("meta 内期望 ',' 或 ')'");
                }
                adv();  // )
                out.emplace_back("meta", DataValue::object(std::move(metaFields)));
                continue;
            }
            if (name == "tags") {
                expectPunct("=");
                if (!isPunct("[")) fail("tags 后应为 [");
                adv();
                std::vector<DataValue> arr;
                while (!isPunct("]")) {
                    if (cur().kind != Tok::Str) fail("tags 元素应为字符串");
                    arr.emplace_back(DataValue::string(adv().value));
                    if (isPunct(",")) adv();
                    else if (!isPunct("]")) fail("tags 内期望 ',' 或 ']'");
                }
                adv();  // ]
                out.emplace_back("tags", DataValue::array(std::move(arr)));
                continue;
            }
            expectPunct("=");
            DataValue v = parseLiteralValue();
            if (name == "weight" || name == "i18n" || name == "id") {
                out.emplace_back(name, std::move(v));
            } else {
                fail("未知属性 '" + name + "'");
            }
        }
    }

    DataValue parseLine(const std::string &poolId, int idx, const DataValue *inheritWhen) {
        const int lineNum = cur().line;
        std::string speaker;
        if (isPunct("-")) {
            adv();
        } else if (isIdent()) {
            if (cur().value == "when") fail("when 分组不允许嵌套");
            speaker = adv().value;
            expectPunct(":");
        } else {
            fail("期望说话人或 '-'");
        }
        if (cur().kind != Tok::Str) fail("期望台词字符串");
        const std::string text = adv().value;

        std::vector<std::pair<std::string, DataValue>> fields;
        fields.emplace_back("speaker", DataValue::string(speaker));
        fields.emplace_back("text", DataValue::string(text));
        if (inheritWhen) fields.emplace_back("when", *inheritWhen);
        std::vector<std::pair<std::string, DataValue>> attrs;
        parseAttrs(lineNum, attrs);
        bool hasId = false;
        for (auto &kv : attrs) {
            if (kv.first == "id") hasId = true;
            fields.push_back(std::move(kv));
        }
        if (!hasId) fields.emplace_back("id", DataValue::string(poolId + "." + std::to_string(idx)));
        return DataValue::object(std::move(fields));
    }

    void parsePool(std::vector<std::pair<std::string, DataValue>> &pools) {
        expectIdent("pool");
        const std::string poolId = expectIdent("pool 名称");
        long long noRepeat = -1;
        while (isIdent() && cur().value == "noRepeat") {
            adv();
            expectPunct("=");
            const Token &t = cur();
            if (t.kind != Tok::Num) fail("noRepeat 应为数字");
            noRepeat = std::strtoll(adv().value.c_str(), nullptr, 10);
        }
        expectPunct("{");

        std::vector<DataValue> lines;
        int idx = 1;
        while (!isPunct("}")) {
            if (cur().kind == Tok::Eof) fail("未闭合的 pool 块");
            if (isIdent() && cur().value == "pool") fail("pool 块不允许嵌套");
            if (isIdent() && cur().value == "when") {
                adv();
                DataValue cond = parseOr();
                expectPunct("{");
                while (!isPunct("}")) {
                    if (cur().kind == Tok::Eof) fail("未闭合的 when 块");
                    lines.push_back(parseLine(poolId, idx, &cond));
                    ++idx;
                }
                adv();
                continue;
            }
            lines.push_back(parseLine(poolId, idx, nullptr));
            ++idx;
        }
        adv();  // }

        std::vector<std::pair<std::string, DataValue>> poolFields;
        poolFields.emplace_back("lines", DataValue::array(std::move(lines)));
        if (noRepeat >= 0) poolFields.emplace_back("noRepeat", DataValue::integer(noRepeat));
        pools.emplace_back(poolId, DataValue::object(std::move(poolFields)));
    }

    DataValue parsePools() {
        std::vector<std::pair<std::string, DataValue>> pools;
        while (cur().kind != Tok::Eof) parsePool(pools);
        return DataValue::object({{"pools", DataValue::object(std::move(pools))}});
    }
};

}  // namespace

bool parseDnut(const std::string &source, const std::string &path, DataValue &outRoot,
               std::string &error) {
    Parser parser(source, path);
    return parser.parse(outRoot, error);
}

}  // namespace eve::dialogue
