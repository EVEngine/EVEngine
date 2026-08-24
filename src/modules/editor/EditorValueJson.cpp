#include "editor/EditorValueJson.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace eve::editor {
namespace {

constexpr std::size_t kMaxJsonDepth = 64;

void appendEscapedString(std::string_view value, std::string& out) {
    constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[byte >> 4]);
                    out.push_back(hex[byte & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    out.push_back('"');
}

void appendJson(const EditorValue& value, std::string& out) {
    std::visit(
        [&out](const auto& current) {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                out += "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out += current ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                char buffer[32];
                const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), current);
                if (error == std::errc()) out.append(buffer, end);
            } else if constexpr (std::is_same_v<T, double>) {
                if (!std::isfinite(current)) {
                    out += "null";
                } else {
                    char buffer[64];
                    const auto [end, error] =
                        std::to_chars(buffer, buffer + sizeof(buffer), current, std::chars_format::general,
                                      std::numeric_limits<double>::max_digits10);
                    if (error == std::errc()) out.append(buffer, end);
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                appendEscapedString(current, out);
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                out.push_back('[');
                bool first = true;
                for (const EditorValue& entry : current) {
                    if (!first) out.push_back(',');
                    first = false;
                    appendJson(entry, out);
                }
                out.push_back(']');
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                out.push_back('{');
                bool first = true;
                for (const auto& [key, entry] : current) {
                    if (!first) out.push_back(',');
                    first = false;
                    appendEscapedString(key, out);
                    out.push_back(':');
                    appendJson(entry, out);
                }
                out.push_back('}');
            }
        },
        value.storage());
}

void appendUtf8(std::uint32_t codePoint, std::string& out) {
    if (codePoint <= 0x7f) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(EditorValue& out) {
        skipWhitespace();
        if (!parseValue(out, 0)) return false;
        skipWhitespace();
        if (position_ != input_.size()) return fail("unexpected trailing JSON content");
        return true;
    }

    const std::string& error() const { return error_; }

private:
    bool parseValue(EditorValue& out, std::size_t depth) {
        if (depth > kMaxJsonDepth) return fail("JSON nesting exceeds 64 levels");
        if (position_ >= input_.size()) return fail("expected a JSON value");
        switch (input_[position_]) {
            case 'n': return parseLiteral("null", EditorValue{}, out);
            case 't': return parseLiteral("true", EditorValue(true), out);
            case 'f': return parseLiteral("false", EditorValue(false), out);
            case '"': {
                std::string value;
                if (!parseString(value)) return false;
                out = EditorValue(std::move(value));
                return true;
            }
            case '[': return parseArray(out, depth + 1);
            case '{': return parseObject(out, depth + 1);
            default:
                if (input_[position_] == '-' || isDigit(input_[position_])) return parseNumber(out);
                return fail("expected a JSON value");
        }
    }

    bool parseLiteral(std::string_view literal, EditorValue value, EditorValue& out) {
        if (input_.substr(position_, literal.size()) != literal) return fail("invalid JSON literal");
        position_ += literal.size();
        out = std::move(value);
        return true;
    }

    bool parseArray(EditorValue& out, std::size_t depth) {
        ++position_;
        skipWhitespace();
        EditorValue::Array result;
        if (consume(']')) {
            out = EditorValue(std::move(result));
            return true;
        }
        while (true) {
            EditorValue entry;
            if (!parseValue(entry, depth)) return false;
            result.push_back(std::move(entry));
            skipWhitespace();
            if (consume(']')) break;
            if (!consume(',')) return fail("expected ',' or ']' in JSON array");
            skipWhitespace();
        }
        out = EditorValue(std::move(result));
        return true;
    }

    bool parseObject(EditorValue& out, std::size_t depth) {
        ++position_;
        skipWhitespace();
        EditorValue::Object result;
        if (consume('}')) {
            out = EditorValue(std::move(result));
            return true;
        }
        while (true) {
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) return fail("expected ':' after JSON object key");
            skipWhitespace();
            EditorValue entry;
            if (!parseValue(entry, depth)) return false;
            result[std::move(key)] = std::move(entry);
            skipWhitespace();
            if (consume('}')) break;
            if (!consume(',')) return fail("expected ',' or '}' in JSON object");
            skipWhitespace();
        }
        out = EditorValue(std::move(result));
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) return fail("expected a JSON string");
        while (position_ < input_.size()) {
            const unsigned char byte = static_cast<unsigned char>(input_[position_++]);
            if (byte == '"') return true;
            if (byte < 0x20) return fail("unescaped control character in JSON string");
            if (byte != '\\') {
                out.push_back(static_cast<char>(byte));
                continue;
            }
            if (position_ >= input_.size()) return fail("unterminated JSON escape");
            switch (input_[position_++]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    if (!parseUnicodeEscape(out)) return false;
                    break;
                default: return fail("invalid JSON escape");
            }
        }
        return fail("unterminated JSON string");
    }

    bool parseUnicodeEscape(std::string& out) {
        std::uint32_t codePoint = 0;
        if (!parseHexQuad(codePoint)) return false;
        if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
            if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u')
                return fail("missing low surrogate in JSON string");
            position_ += 2;
            std::uint32_t low = 0;
            if (!parseHexQuad(low)) return false;
            if (low < 0xdc00 || low > 0xdfff) return fail("invalid low surrogate in JSON string");
            codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
        } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
            return fail("unexpected low surrogate in JSON string");
        }
        appendUtf8(codePoint, out);
        return true;
    }

    bool parseHexQuad(std::uint32_t& value) {
        if (position_ + 4 > input_.size()) return fail("incomplete Unicode escape in JSON string");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char digit = input_[position_++];
            value <<= 4;
            if (digit >= '0' && digit <= '9')
                value |= static_cast<std::uint32_t>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            else
                return fail("invalid Unicode escape in JSON string");
        }
        return true;
    }

    bool parseNumber(EditorValue& out) {
        const std::size_t start = position_;
        consume('-');
        if (consume('0')) {
            if (position_ < input_.size() && isDigit(input_[position_])) return fail("leading zero in JSON number");
        } else {
            if (position_ >= input_.size() || !isDigit(input_[position_])) return fail("invalid JSON number");
            while (position_ < input_.size() && isDigit(input_[position_])) ++position_;
        }

        bool integer = true;
        if (consume('.')) {
            integer = false;
            if (position_ >= input_.size() || !isDigit(input_[position_])) return fail("invalid JSON fraction");
            while (position_ < input_.size() && isDigit(input_[position_])) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            integer = false;
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (position_ >= input_.size() || !isDigit(input_[position_])) return fail("invalid JSON exponent");
            while (position_ < input_.size() && isDigit(input_[position_])) ++position_;
        }

        const std::string_view token = input_.substr(start, position_ - start);
        if (integer) {
            std::int64_t value      = 0;
            const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
            if (error == std::errc() && end == token.data() + token.size()) {
                out = EditorValue(value);
                return true;
            }
        }
        double value            = 0.0;
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (error != std::errc() || end != token.data() + token.size() || !std::isfinite(value))
            return fail("JSON number is outside the supported range");
        out = EditorValue(value);
        return true;
    }

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\n' && value != '\r') break;
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool fail(std::string_view message) {
        if (error_.empty()) error_ = std::string(message) + " at byte " + std::to_string(position_);
        return false;
    }

    static bool isDigit(char value) { return value >= '0' && value <= '9'; }

    std::string_view input_;
    std::size_t      position_ = 0;
    std::string      error_;
};

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

std::string editorValueToJson(const EditorValue& value) {
    std::string json;
    appendJson(value, json);
    return json;
}

EditorResult<EditorValue> editorValueFromJson(const std::string& json) {
    JsonParser  parser(json);
    EditorValue value;
    if (!parser.parse(value))
        return EditorResult<EditorValue>::error(EditorStatus::Rejected, RuleId("editor.value.invalid-json"),
                                                parser.error());
    return EditorResult<EditorValue>::applied(std::move(value));
}

std::string editorValueContentHash(const EditorValue& value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << fnv1a(editorValueToJson(value));
    return stream.str();
}

}  // namespace eve::editor
