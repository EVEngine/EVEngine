#include "common/Json.h"

#include "common/Value.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eve::json {

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

struct Node {
    enum class Kind { Null, Bool, Number, String, Object, Array };

    Kind   kind      = Kind::Null;
    bool   boolVal   = false;
    double numberVal = 0.0;
    /** Set when the literal had no fraction or exponent. */
    bool integerLiteral = false;
    /** Set when the integer literal fits the canonical Int64 range. */
    bool                     hasInt64 = false;
    std::int64_t             intVal   = 0;
    std::string              stringVal;
    std::vector<std::string> memberNames;   // object keys, document order
    std::vector<Node>        memberValues;  // object values, parallel to memberNames
    std::vector<Node>        elements;      // array
};

namespace {

/**
 * Recursive-descent JSON parser. Promoted from the module-local parser in
 * i18n/I18n.cpp, plus integral-literal tracking so getInt() is exact.
 */
class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool parse(Node& out, std::string* error) {
        skipWs();
        if (!parseValue(out)) {
            if (error) *error = "invalid JSON near offset " + std::to_string(pos_);
            return false;
        }
        skipWs();
        if (pos_ != s_.size()) {
            if (error) *error = "trailing data at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    static constexpr size_t kMaxDepth = 256;
    const std::string&      s_;
    size_t                  pos_ = 0;

    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool peek(char c) const { return pos_ < s_.size() && s_[pos_] == c; }

    bool parseValue(Node& out, size_t depth = 0) {
        if (depth > kMaxDepth) return false;
        if (pos_ >= s_.size()) return false;
        switch (s_[pos_]) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"':
                if (!parseString(out.stringVal)) return false;
                out.kind = Node::Kind::String;
                return true;
            case 't': return parseLiteral("true", out, true);
            case 'f': return parseLiteral("false", out, false);
            case 'n': return parseNull(out);
            default: return parseNumber(out);
        }
    }

    bool parseLiteral(const char* lit, Node& out, bool value) {
        const size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) != 0) return false;
        pos_ += n;
        out.kind    = Node::Kind::Bool;
        out.boolVal = value;
        return true;
    }

    bool parseNull(Node& out) {
        if (s_.compare(pos_, 4, "null") != 0) return false;
        pos_ += 4;
        out.kind = Node::Kind::Null;
        return true;
    }

    bool parseNumber(Node& out) {
        const size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size()) return false;
        if (s_[pos_] == '0') {
            ++pos_;
            // RFC 8259 forbids leading zeroes in the integer component.
            if (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') return false;
        } else if (s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        } else {
            return false;
        }
        bool integral = true;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            integral = false;
            ++pos_;
            const size_t fractionStart = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
            if (pos_ == fractionStart) return false;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            integral = false;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
            const size_t exponentStart = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
            if (pos_ == exponentStart) return false;
        }
        const std::string num = s_.substr(start, pos_ - start);
        try {
            size_t used   = 0;
            out.numberVal = std::stod(num, &used);
            if (used != num.size() || !std::isfinite(out.numberVal)) return false;
            out.integerLiteral = integral;
            if (integral) {
                try {
                    out.intVal   = std::stoll(num);
                    out.hasInt64 = true;
                } catch (...) {
                    // Keep the finite double for compatibility with the
                    // lenient read-only JSON facade. Owning Value conversion
                    // rejects this integer because it cannot preserve Int64.
                    out.hasInt64 = false;
                }
            }
        } catch (...) {
            // Out of long long range but still a valid double (or vice versa):
            // keep whichever conversion succeeded.
            if (!integral) return false;
            try {
                out.numberVal = std::stod(num);
            } catch (...) {
                return false;
            }
        }
        out.kind = Node::Kind::Number;
        return true;
    }

    bool parseString(std::string& out) {
        if (!peek('"')) return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20) return false;
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) return false;
            const char esc = s_[pos_++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return false;
                    const char hex[5] = {s_[pos_], s_[pos_ + 1], s_[pos_ + 2], s_[pos_ + 3], '\0'};
                    pos_ += 4;
                    char*          end = nullptr;
                    const unsigned cp  = static_cast<unsigned>(std::strtoul(hex, &end, 16));
                    if (!end || *end != '\0') return false;
                    // A high surrogate followed by "\uXXXX" forms one code point.
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                        s_[pos_ + 1] == 'u') {
                        const char lohex[5] = {s_[pos_ + 2], s_[pos_ + 3], s_[pos_ + 4], s_[pos_ + 5], '\0'};
                        pos_ += 6;
                        char*          loEnd = nullptr;
                        const unsigned lo    = static_cast<unsigned>(std::strtoul(lohex, &loEnd, 16));
                        if (loEnd && *loEnd == '\0' && lo >= 0xDC00 && lo <= 0xDFFF) {
                            appendUtf8(out, 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00));
                        } else {
                            return false;
                        }
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        return false;
                    } else {
                        appendUtf8(out, cp);
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;  // unterminated
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseObject(Node& out, size_t depth) {
        ++pos_;  // '{'
        skipWs();
        if (peek('}')) {
            ++pos_;
            out.kind = Node::Kind::Object;
            return true;
        }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            for (const std::string& existing : out.memberNames)
                if (existing == key) return false;
            skipWs();
            if (!peek(':')) return false;
            ++pos_;
            skipWs();
            Node val;
            if (!parseValue(val, depth + 1)) return false;
            out.memberNames.emplace_back(std::move(key));
            out.memberValues.emplace_back(std::move(val));
            skipWs();
            if (peek('}')) {
                ++pos_;
                out.kind = Node::Kind::Object;
                return true;
            }
            if (!peek(',')) return false;
            ++pos_;
        }
    }

    bool parseArray(Node& out, size_t depth) {
        ++pos_;  // '['
        skipWs();
        if (peek(']')) {
            ++pos_;
            out.kind = Node::Kind::Array;
            return true;
        }
        while (true) {
            skipWs();
            Node val;
            if (!parseValue(val, depth + 1)) return false;
            out.elements.push_back(std::move(val));
            skipWs();
            if (peek(']')) {
                ++pos_;
                out.kind = Node::Kind::Array;
                return true;
            }
            if (!peek(',')) return false;
            ++pos_;
        }
    }
};

/** Compact double formatting; matches what ostringstream default produces. */
std::string numberToString(const Node& n) {
    if (n.hasInt64) return std::to_string(n.intVal);
    std::ostringstream os;
    os << n.numberVal;
    return os.str();
}

bool stringToDouble(const std::string& s, double& out) {
    try {
        size_t used = 0;
        const double v = std::stod(s, &used);
        while (used < s.size() && (s[used] == ' ' || s[used] == '\t')) ++used;
        if (used != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

bool Value::isNull() const { return !node_ || node_->kind == Node::Kind::Null; }
bool Value::isBool() const { return node_ && node_->kind == Node::Kind::Bool; }
bool Value::isNumber() const { return node_ && node_->kind == Node::Kind::Number; }
bool Value::isInt64() const { return node_ && node_->kind == Node::Kind::Number && node_->hasInt64; }
bool Value::isIntegerLiteral() const { return node_ && node_->kind == Node::Kind::Number && node_->integerLiteral; }
bool Value::isString() const { return node_ && node_->kind == Node::Kind::String; }
bool Value::isObject() const { return node_ && node_->kind == Node::Kind::Object; }
bool Value::isArray() const { return node_ && node_->kind == Node::Kind::Array; }

bool Value::has(const char* key) const { return static_cast<bool>(get(key)); }

Value Value::get(const char* key) const {
    if (!node_ || node_->kind != Node::Kind::Object || !key) return Value();
    for (size_t i = 0; i < node_->memberNames.size(); ++i)
        if (node_->memberNames[i] == key) return Value(&node_->memberValues[i]);
    return Value();
}

std::vector<std::string> Value::keys() const {
    std::vector<std::string> out;
    if (!node_ || node_->kind != Node::Kind::Object) return out;
    out.reserve(node_->memberNames.size());
    for (const auto& name : node_->memberNames) out.push_back(name);
    return out;
}

size_t Value::size() const {
    if (!node_) return 0;
    if (node_->kind == Node::Kind::Array) return node_->elements.size();
    if (node_->kind == Node::Kind::Object) return node_->memberValues.size();
    return 0;
}

Value Value::at(size_t index) const {
    if (!node_ || node_->kind != Node::Kind::Array || index >= node_->elements.size())
        return Value();
    return Value(&node_->elements[index]);
}

bool Value::asBool(bool fallback) const {
    if (!node_) return fallback;
    switch (node_->kind) {
        case Node::Kind::Bool: return node_->boolVal;
        case Node::Kind::Number: return node_->numberVal != 0.0;
        case Node::Kind::String:
            if (node_->stringVal == "true") return true;
            if (node_->stringVal == "false") return false;
            return fallback;
        default: return fallback;
    }
}

double Value::asDouble(double fallback) const {
    if (!node_) return fallback;
    switch (node_->kind) {
        case Node::Kind::Number: return node_->numberVal;
        case Node::Kind::Bool: return node_->boolVal ? 1.0 : 0.0;
        case Node::Kind::String: {
            double v = 0.0;
            return stringToDouble(node_->stringVal, v) ? v : fallback;
        }
        default: return fallback;
    }
}

std::int64_t Value::asInt64(std::int64_t fallback) const { return isInt64() ? node_->intVal : fallback; }

int Value::asInt(int fallback) const {
    if (!node_) return fallback;
    if (node_->kind == Node::Kind::Number && node_->hasInt64) {
        if (node_->intVal < std::numeric_limits<int>::min() ||
            node_->intVal > std::numeric_limits<int>::max())
            return fallback;
        return static_cast<int>(node_->intVal);
    }
    const double d = asDouble(static_cast<double>(fallback));
    if (!std::isfinite(d) || d < static_cast<double>(std::numeric_limits<int>::min()) ||
        d > static_cast<double>(std::numeric_limits<int>::max()))
        return fallback;
    return static_cast<int>(d);
}

float Value::asFloat(float fallback) const {
    return static_cast<float>(asDouble(static_cast<double>(fallback)));
}

std::string Value::asString(const std::string& fallback) const {
    if (!node_) return fallback;
    switch (node_->kind) {
        case Node::Kind::String: return node_->stringVal;
        case Node::Kind::Number: return numberToString(*node_);
        case Node::Kind::Bool: return node_->boolVal ? "true" : "false";
        default: return fallback;
    }
}

bool Value::getBool(const char* key, bool fallback) const { return get(key).asBool(fallback); }
int Value::getInt(const char* key, int fallback) const { return get(key).asInt(fallback); }
float Value::getFloat(const char* key, float fallback) const { return get(key).asFloat(fallback); }
double Value::getDouble(const char* key, double fallback) const {
    return get(key).asDouble(fallback);
}
std::string Value::getString(const char* key, const std::string& fallback) const {
    return get(key).asString(fallback);
}

std::vector<std::string> Value::toStringArray() const {
    std::vector<std::string> out;
    if (!node_ || node_->kind != Node::Kind::Array) return out;
    out.reserve(node_->elements.size());
    for (const auto& e : node_->elements) out.push_back(Value(&e).asString());
    return out;
}

std::vector<std::string> Value::getStringArray(const char* key) const {
    return get(key).toStringArray();
}

std::vector<int> Value::getIntArray(const char* key) const {
    std::vector<int> out;
    const Value arr = get(key);
    const size_t n = arr.isArray() ? arr.size() : 0;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(arr.at(i).asInt(0));
    return out;
}

std::vector<float> Value::getFloatArray(const char* key) const {
    std::vector<float> out;
    const Value arr = get(key);
    const size_t n = arr.isArray() ? arr.size() : 0;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(arr.at(i).asFloat(0.f));
    return out;
}

std::unordered_map<std::string, std::string> Value::getStringMap(const char* key) const {
    std::unordered_map<std::string, std::string> out;
    const Value obj = get(key);
    if (!obj.isObject()) return out;
    for (const auto& name : obj.keys()) out[name] = obj.getString(name.c_str());
    return out;
}

std::unordered_map<std::string, int> Value::getIntMap(const char* key) const {
    std::unordered_map<std::string, int> out;
    const Value obj = get(key);
    if (!obj.isObject()) return out;
    for (const auto& name : obj.keys()) out[name] = obj.getInt(name.c_str(), 0);
    return out;
}

namespace {

void appendEscapedString(std::string_view value, std::string& output) {
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (byte < 0x20) {
                    output += "\\u00";
                    output.push_back(hex[byte >> 4]);
                    output.push_back(hex[byte & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    output.push_back('"');
}

bool appendCanonicalJson(const eve::Value& value, std::string& output) {
    return std::visit(
        [&output](const auto& current) -> bool {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                output += "null";
                return true;
            } else if constexpr (std::is_same_v<T, bool>) {
                output += current ? "true" : "false";
                return true;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                char buffer[32];
                const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), current);
                if (error != std::errc()) return false;
                output.append(buffer, end);
                return true;
            } else if constexpr (std::is_same_v<T, double>) {
                if (!std::isfinite(current)) return false;
                std::ostringstream stream;
                stream.imbue(std::locale::classic());
                stream.precision(std::numeric_limits<double>::max_digits10);
                stream << current;
                const std::string number = stream.str();
                output += number;
                if (number.find_first_of(".eE") == std::string_view::npos) output += ".0";
                return true;
            } else if constexpr (std::is_same_v<T, std::string>) {
                appendEscapedString(current, output);
                return true;
            } else if constexpr (std::is_same_v<T, eve::Value::Array>) {
                output.push_back('[');
                bool first = true;
                for (const eve::Value& element : current) {
                    if (!first) output.push_back(',');
                    first = false;
                    if (!appendCanonicalJson(element, output)) return false;
                }
                output.push_back(']');
                return true;
            } else if constexpr (std::is_same_v<T, eve::Value::Object>) {
                output.push_back('{');
                bool first = true;
                for (const auto& [key, element] : current) {
                    if (!first) output.push_back(',');
                    first = false;
                    appendEscapedString(key, output);
                    output.push_back(':');
                    if (!appendCanonicalJson(element, output)) return false;
                }
                output.push_back('}');
                return true;
            }
        },
        value.storage());
}

}  // namespace

Result<std::string> stringify(const eve::Value& value) {
    std::string output;
    if (!appendCanonicalJson(value, output)) {
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::SerializationError, "Value contains a non-finite Double; JSON requires finite numbers"));
    }
    return Result<std::string>::success(std::move(output));
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

Document::Document() = default;
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

Document Document::parse(const std::string& text, std::string* error) {
    Document doc;
    auto node = std::make_unique<Node>();
    Parser parser(text);
    if (!parser.parse(*node, error)) return doc;
    doc.root_ = std::move(node);
    return doc;
}

}  // namespace eve::json
