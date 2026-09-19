#include "dialogue/DnutParser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

enum class Tok { Ident, Str, Num, Punct, Eof };

struct Token {
    Tok kind = Tok::Eof;
    std::string value;
    int line = 1;
    int column = 1;
};

std::string floatToString(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string scalarToString(const DataValue &v) {
    switch (v.kind()) {
        case DataValue::Kind::String: return v.asString();
        case DataValue::Kind::Int: return std::to_string(v.asInt());
        case DataValue::Kind::Float: return floatToString(v.asDouble());
        case DataValue::Kind::Bool: return v.asBool() ? "true" : "false";
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
    if (v.kind() == DataValue::Kind::Int) return DataValue::integer(-v.asInt());
    return DataValue::number(-v.asDouble());
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

    bool parseDocument(DnutDocument& out, std::vector<ConversationDiagnostic>& diagnostics) {
        try {
            tokenize();
            if (!isIdent() || cur().value != "schema") fail("期望文件 schema");
            adv();
            if (cur().kind != Tok::Str) fail("schema 应为字符串");
            out.schema = adv().value;
            if (out.schema != "eve.dnut") fail("不支持的 dnut schema '" + out.schema + "'");
            if (!isIdent() || cur().value != "version") fail("期望文件 version");
            adv();
            if (cur().kind != Tok::Num) fail("version 应为整数");
            out.version = static_cast<int>(std::strtol(adv().value.c_str(), nullptr, 10));
            if (out.version != DnutDocument::CurrentVersion) fail("不支持的 dnut version");
            DataValue::Object pools;
            while (cur().kind != Tok::Eof) {
                if (!isIdent()) fail("期望 pool 或 conversation");
                if (cur().value == "pool") parsePool(pools);
                else if (cur().value == "conversation") out.conversations.push_back(parseConversation());
                else fail("未知顶层字段 '" + cur().value + "'");
            }
            out.poolRoot = DataValue::object({{"pools", DataValue::object(std::move(pools))}});
            return true;
        } catch (const ParseError& e) {
            const int line = toks_.empty() ? 1 : cur().line;
            const int column = toks_.empty() ? 1 : cur().column;
            diagnostics.push_back({ConversationDiagnostic::Severity::Error, path_, line, e.what(),
                                   "DnutParseError", column, {}});
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
        size_t lineStart = 0;
        int line = 1;

        while (i < n) {
            const int column = static_cast<int>(i - lineStart + 1);
            const auto add = [&](Tok k, std::string v) {
                toks_.push_back(Token{k, std::move(v), line, column});
            };
            const char c = src[i];
            if (c == '\n') {
                ++line;
                ++i;
                lineStart = i;
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
                    if (src[i] == '\n') {
                        ++line;
                        lineStart = i + 1;
                    }
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
        toks_.push_back(Token{Tok::Eof, "", line, static_cast<int>(i - lineStart + 1)});
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
            if (left.kind() == DataValue::Kind::Object) {
                for (auto &kv : *left.getIf<DataValue::Object>()) {
                    if (kv.first == "all" && kv.second.kind() == DataValue::Kind::Array) {
                        kv.second.pushBack(std::move(right));
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
            if (left.kind() == DataValue::Kind::Object) {
                for (auto &kv : *left.getIf<DataValue::Object>()) {
                    if (kv.first == "any" && kv.second.kind() == DataValue::Kind::Array) {
                        kv.second.pushBack(std::move(right));
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

    void parseAttrs(const int lineNum, DataValue::Object &out) {
        while (cur().line == lineNum && !isPunct("}") && cur().kind != Tok::Eof) {
            if (!isIdent()) fail("期望属性名");
            const std::string name = adv().value;
            if (name == "meta" && isPunct("(")) {
                adv();
                DataValue::Object metaFields;
                while (!isPunct(")")) {
                    if (!isIdent()) fail("meta 键应为标识符");
                    const std::string k = adv().value;
                    expectPunct("=");
                    DataValue v = parseLiteralValue();
                    if (v.kind() != DataValue::Kind::String && v.kind() != DataValue::Kind::Int &&
                        v.kind() != DataValue::Kind::Float && v.kind() != DataValue::Kind::Bool)
                        fail("meta 值只支持标量");
                    metaFields.emplace(k, DataValue::string(scalarToString(v)));
                    if (isPunct(",")) adv();
                    else if (!isPunct(")")) fail("meta 内期望 ',' 或 ')'");
                }
                adv();  // )
                out.emplace("meta", DataValue::object(std::move(metaFields)));
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
                out.emplace("tags", DataValue::array(std::move(arr)));
                continue;
            }
            expectPunct("=");
            DataValue v = parseLiteralValue();
            if (name == "weight" || name == "i18n" || name == "id") {
                out.emplace(name, std::move(v));
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

        DataValue::Object fields;
        fields.emplace("speaker", DataValue::string(speaker));
        fields.emplace("text", DataValue::string(text));
        if (inheritWhen) fields.emplace("when", *inheritWhen);
        DataValue::Object attrs;
        parseAttrs(lineNum, attrs);
        bool hasId = false;
        for (auto &kv : attrs) {
            if (kv.first == "id") hasId = true;
            fields.emplace(std::move(kv.first), std::move(kv.second));
        }
        if (!hasId) fields.emplace("id", DataValue::string(poolId + "." + std::to_string(idx)));
        return DataValue::object(std::move(fields));
    }

    void parsePool(DataValue::Object &pools) {
        expectIdent("pool");
        const std::string poolId = expectIdent("pool 名称");
        if (pools.find(poolId) != pools.end()) fail("重复 pool ID '" + poolId + "'");
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

        DataValue::Object poolFields;
        poolFields.emplace("lines", DataValue::array(std::move(lines)));
        if (noRepeat >= 0) poolFields.emplace("noRepeat", DataValue::integer(noRepeat));
        pools.emplace(poolId, DataValue::object(std::move(poolFields)));
    }

    std::string scalarText() {
        if (cur().kind == Tok::Str || cur().kind == Tok::Ident || cur().kind == Tok::Num)
            return adv().value;
        fail("期望标量值");
    }

    DataValue parseObjectArguments() {
        expectPunct("(");
        DataValue::Object fields;
        while (!isPunct(")")) {
            const std::string key = expectIdent("参数名");
            expectPunct("=");
            fields.emplace(key, parseLiteralValue());
            if (isPunct(",")) adv();
            else if (!isPunct(")")) fail("参数列表中期望 ',' 或 ')'");
        }
        adv();
        return DataValue::object(std::move(fields));
    }

    PaymentSpec parsePayment() {
        auto parsed = PaymentSpec::fromValue(parseObjectArguments());
        if (!parsed) fail(parsed.status().describe());
        return std::move(parsed).takeValue();
    }

    eve::StateMutation parseMutation() {
        DataValue fields = parseObjectArguments();
        const DataValue* subject = fields.find("subject");
        const DataValue* key = fields.find("key");
        const DataValue* kind = fields.find("kind");
        const DataValue* value = fields.find("value");
        const DataValue* persistent = fields.find("persistent");
        for (const auto& field : fields.keys())
            if (field != "subject" && field != "key" && field != "kind" && field != "value" &&
                field != "persistent")
                fail("未知 mutation 字段 '" + field + "'");
        if (!subject || !subject->isString() || subject->asString().empty())
            fail("mutation.subject 必须是非空字符串");
        if (!key || !key->isString() || key->asString().empty())
            fail("mutation.key 必须是非空字符串");
        if (!kind || !kind->isString()) fail("mutation.kind 必须是字符串");
        eve::StateMutation mutation;
        mutation.subject = subject->asString();
        mutation.key = key->asString();
        if (kind->asString() == "set") mutation.kind = eve::MutationKind::Set;
        else if (kind->asString() == "remove") mutation.kind = eve::MutationKind::Remove;
        else if (kind->asString() == "addTag") mutation.kind = eve::MutationKind::AddTag;
        else if (kind->asString() == "removeTag") mutation.kind = eve::MutationKind::RemoveTag;
        else if (kind->asString() == "addNumber") mutation.kind = eve::MutationKind::AddNumber;
        else fail("mutation.kind 不受支持 '" + kind->asString() + "'");
        if (mutation.kind == eve::MutationKind::Set || mutation.kind == eve::MutationKind::AddNumber) {
            if (!value) fail("set/addNumber mutation 缺少 value");
            mutation.value = *value;
        } else if (value) {
            fail("remove/tag mutation 不允许 value");
        }
        if (persistent) {
            if (!persistent->isBool()) fail("mutation.persistent 必须是 bool");
            mutation.persistent = persistent->asBool();
        }
        return mutation;
    }

    void parseNodeAttributes(ConversationAsset::Node& node, int line) {
        static const std::unordered_set<std::string> allowed = {
            "next", "speaker", "text", "pool", "i18n", "voice", "target", "return", "result", "kind",
            "arguments", "payment", "mutation"};
        while (cur().kind != Tok::Eof && cur().line == line && !isPunct("{") && !isPunct("}")) {
            const std::string key = expectIdent("node 属性");
            if (!allowed.contains(key)) fail("未知 node 字段 '" + key + "'");
            if (key == "arguments") {
                node.arguments = toDialogueStateValue(parseObjectArguments());
                continue;
            }
            if (key == "payment") {
                node.payment = parsePayment();
                continue;
            }
            if (key == "mutation") {
                node.stateMutations.push_back(parseMutation());
                continue;
            }
            expectPunct("=");
            const std::string value = scalarText();
            if (key == "next") node.next = value;
            else if (key == "speaker") node.speaker = value;
            else if (key == "text") node.text = value;
            else if (key == "pool") node.pool = value;
            else if (key == "i18n") node.i18nKey = value;
            else if (key == "voice") node.voice = value;
            else if (key == "target") node.target = value;
            else if (key == "return") node.returnNode = value;
            else if (key == "result") node.expression = value;
            else if (key == "kind") {
                if (value == "operation") node.commandKind = CommandRequestKind::Operation;
                else if (value == "gameplay") node.commandKind = CommandRequestKind::GameplayAction;
                else fail("command kind 只支持 operation 或 gameplay");
            }
        }
    }

    ConversationRoute parseRoute() {
        const int line = cur().line;
        expectIdent("route");
        ConversationRoute route;
        route.first = expectIdent("稳定 route ID");
        bool hasTarget = false;
        while (cur().kind != Tok::Eof && cur().line == line && !isPunct("}")) {
            const std::string key = expectIdent("route 字段");
            if (key != "target" && key != "when" && key != "text" && key != "i18n" && key != "payment" &&
                key != "mutation")
                fail("未知 route 字段 '" + key + "'");
            if (key == "payment") {
                route.payment = parsePayment();
                continue;
            }
            if (key == "mutation") {
                route.stateMutations.push_back(parseMutation());
                continue;
            }
            expectPunct("=");
            const std::string value = scalarText();
            if (key == "target") { route.second = value; hasTarget = true; }
            else if (key == "when") route.expression = value;
            else if (key == "text") route.text = value;
            else if (key == "i18n") route.i18nKey = value;
        }
        if (!hasTarget) fail("route 缺少 target");
        return route;
    }

    ConversationAsset::Node parseNode() {
        const int line = cur().line;
        expectIdent("node");
        ConversationAsset::Node node;
        node.id = expectIdent("node ID");
        const std::string kind = expectIdent("node 类型");
        if (kind == "line") node.kind = ConversationAsset::Node::Kind::Line;
        else if (kind == "branch") node.kind = ConversationAsset::Node::Kind::Branch;
        else if (kind == "choice") node.kind = ConversationAsset::Node::Kind::Choice;
        else if (kind == "call") node.kind = ConversationAsset::Node::Kind::Call;
        else if (kind == "command") node.kind = ConversationAsset::Node::Kind::Command;
        else if (kind == "wait") node.kind = ConversationAsset::Node::Kind::Wait;
        else if (kind == "end") node.kind = ConversationAsset::Node::Kind::End;
        else fail("未知 node 类型 '" + kind + "'");
        parseNodeAttributes(node, line);
        if (isPunct("{")) {
            adv();
            while (!isPunct("}")) {
                if (cur().kind == Tok::Eof) fail("未闭合的 node 块");
                if (!isIdent() || cur().value != "route") fail("node 块只允许 route");
                node.routes.push_back(parseRoute());
            }
            adv();
        }
        return node;
    }

    ConversationAsset parseConversation() {
        const int declarationLine = cur().line;
        expectIdent("conversation");
        ConversationAsset asset;
        asset.id = expectIdent("conversation ID");
        while (cur().kind != Tok::Eof && cur().line == declarationLine && !isPunct("{")) {
            const std::string key = expectIdent("conversation 字段");
            if (key != "entry" && key != "version") fail("未知 conversation 字段 '" + key + "'");
            expectPunct("=");
            const std::string value = scalarText();
            if (key == "entry") asset.entry = value;
            else asset.version = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
        }
        expectPunct("{");
        while (!isPunct("}")) {
            if (cur().kind == Tok::Eof) fail("未闭合的 conversation 块");
            if (!isIdent()) fail("conversation 内期望 parameter 或 node");
            if (cur().value == "parameter") {
                const int line = cur().line;
                adv();
                const std::string name = expectIdent("parameter 名称");
                const std::string type = expectIdent("parameter 类型");
                ConversationAsset::Parameter parameter;
                parameter.name = name;
                if (type != "string" && type != "int" && type != "float" && type != "bool")
                    fail("未知 parameter 类型 '" + type + "'");
                if (type == "string") parameter.type = ConversationAsset::Parameter::Type::String;
                else if (type == "int") parameter.type = ConversationAsset::Parameter::Type::Int;
                else if (type == "float") parameter.type = ConversationAsset::Parameter::Type::Float;
                else parameter.type = ConversationAsset::Parameter::Type::Bool;
                while (cur().line == line && cur().kind != Tok::Eof) {
                    const std::string field = expectIdent("parameter 字段");
                    if (field == "required") parameter.required = true;
                    else if (field == "optional") parameter.required = false;
                    else if (field == "default") {
                        expectPunct("=");
                        parameter.defaultValue = toDialogueStateValue(parseLiteralValue());
                        parameter.required = false;
                    } else fail("未知 parameter 字段 '" + field + "'");
                }
                asset.parameters.push_back(std::move(parameter));
            } else if (cur().value == "node") {
                asset.nodes.push_back(parseNode());
            } else {
                fail("未知 conversation 字段 '" + cur().value + "'");
            }
        }
        adv();
        return asset;
    }

    DataValue parsePools() {
        DataValue::Object pools;
        while (cur().kind != Tok::Eof) parsePool(pools);
        return DataValue::object({{"pools", DataValue::object(std::move(pools))}});
    }
};

}  // namespace

bool parseDnut(const std::string &source, const std::string &path, DataValue &outRoot,
               std::string &error) {
    Parser parser(source, path);
    DnutDocument document;
    std::vector<ConversationDiagnostic> diagnostics;
    if (!parser.parseDocument(document, diagnostics)) {
        error = diagnostics.empty() ? path + ": dnut parse failed" : diagnostics.front().message;
        return false;
    }
    outRoot = std::move(document.poolRoot);
    return true;
}

bool parseDnutDocument(const std::string& source, const std::string& path, DnutDocument& out,
                       std::vector<ConversationDiagnostic>& diagnostics) {
    Parser parser(source, path);
    return parser.parseDocument(out, diagnostics);
}

}  // namespace eve::dialogue
