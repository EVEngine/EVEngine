#include "common/Xml.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <utility>

namespace eve::xml {

struct Node {
    std::string                              tag;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<Node>>       children;
    Node*                                    parent = nullptr;
};

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool parse(std::unique_ptr<Node>& out, std::string* error) {
        skipMisc();
        if (!parseElement(out, nullptr, 0)) {
            if (error && error->empty())
                *error = "invalid XML near offset " + std::to_string(pos_);
            return false;
        }
        skipMisc();
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
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool startsWith(const char* lit) const {
        const size_t n = std::char_traits<char>::length(lit);
        return pos_ + n <= s_.size() && s_.compare(pos_, n, lit) == 0;
    }

    bool skipComment() {
        if (!startsWith("<!--")) return false;
        pos_ += 4;
        while (pos_ + 2 < s_.size()) {
            if (s_[pos_] == '-' && s_[pos_ + 1] == '-' && s_[pos_ + 2] == '>') {
                pos_ += 3;
                return true;
            }
            ++pos_;
        }
        return false;
    }

    bool skipProcessingInstruction() {
        if (!startsWith("<?")) return false;
        pos_ += 2;
        while (pos_ + 1 < s_.size()) {
            if (s_[pos_] == '?' && s_[pos_ + 1] == '>') {
                pos_ += 2;
                return true;
            }
            ++pos_;
        }
        return false;
    }

    bool skipDoctype() {
        if (!startsWith("<!DOCTYPE") && !startsWith("<!doctype")) return false;
        pos_ += 9;
        int depth = 1;
        while (pos_ < s_.size() && depth > 0) {
            if (s_[pos_] == '<')
                ++depth;
            else if (s_[pos_] == '>')
                --depth;
            ++pos_;
        }
        return depth == 0;
    }

    void skipMisc() {
        for (;;) {
            skipWs();
            if (skipComment() || skipProcessingInstruction() || skipDoctype()) continue;
            break;
        }
    }

    bool parseName(std::string& out) {
        if (pos_ >= s_.size()) return false;
        const unsigned char c0 = static_cast<unsigned char>(s_[pos_]);
        if (!(std::isalpha(c0) || c0 == '_' || c0 == ':')) return false;
        const size_t start = pos_++;
        while (pos_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[pos_]);
            if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':')
                ++pos_;
            else
                break;
        }
        out = s_.substr(start, pos_ - start);
        return !out.empty();
    }

    bool decodeEntity(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '&') return false;
        ++pos_;
        if (startsWith("amp;")) {
            pos_ += 4;
            out += '&';
            return true;
        }
        if (startsWith("lt;")) {
            pos_ += 3;
            out += '<';
            return true;
        }
        if (startsWith("gt;")) {
            pos_ += 3;
            out += '>';
            return true;
        }
        if (startsWith("quot;")) {
            pos_ += 5;
            out += '"';
            return true;
        }
        if (startsWith("apos;")) {
            pos_ += 5;
            out += '\'';
            return true;
        }
        if (pos_ < s_.size() && s_[pos_] == '#') {
            ++pos_;
            unsigned long code = 0;
            if (pos_ < s_.size() && (s_[pos_] == 'x' || s_[pos_] == 'X')) {
                ++pos_;
                const size_t start = pos_;
                while (pos_ < s_.size() && std::isxdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
                if (pos_ == start || pos_ >= s_.size() || s_[pos_] != ';') return false;
                code = std::strtoul(s_.substr(start, pos_ - start).c_str(), nullptr, 16);
            } else {
                const size_t start = pos_;
                while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
                if (pos_ == start || pos_ >= s_.size() || s_[pos_] != ';') return false;
                code = std::strtoul(s_.substr(start, pos_ - start).c_str(), nullptr, 10);
            }
            ++pos_;  // ';'
            if (code == 0 || code > 0x10FFFFul) return false;
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            return true;
        }
        return false;
    }

    bool parseAttributeValue(std::string& out) {
        if (pos_ >= s_.size()) return false;
        const char quote = s_[pos_];
        if (quote != '"' && quote != '\'') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == quote) {
                ++pos_;
                return true;
            }
            if (c == '&') {
                if (!decodeEntity(out)) return false;
                continue;
            }
            if (c == '<') return false;
            out += c;
            ++pos_;
        }
        return false;
    }

    bool parseAttributes(Node& node) {
        for (;;) {
            skipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == '/' || s_[pos_] == '>') return true;
            std::string name;
            if (!parseName(name)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '=') return false;
            ++pos_;
            skipWs();
            std::string value;
            if (!parseAttributeValue(value)) return false;
            node.attributes[std::move(name)] = std::move(value);
        }
    }

    bool skipTextUntilTag() {
        while (pos_ < s_.size() && s_[pos_] != '<') {
            if (s_[pos_] == '&') {
                std::string discard;
                if (!decodeEntity(discard)) return false;
            } else {
                ++pos_;
            }
        }
        return true;
    }

    bool parseElement(std::unique_ptr<Node>& out, Node* parent, size_t depth) {
        if (depth > kMaxDepth) return false;
        skipMisc();
        if (pos_ >= s_.size() || s_[pos_] != '<') return false;
        if (startsWith("</") || startsWith("<?") || startsWith("<!")) return false;
        ++pos_;
        auto node = std::make_unique<Node>();
        node->parent = parent;
        if (!parseName(node->tag)) return false;
        if (!parseAttributes(*node)) return false;
        skipWs();
        if (pos_ >= s_.size()) return false;
        if (s_[pos_] == '/') {
            ++pos_;
            if (pos_ >= s_.size() || s_[pos_] != '>') return false;
            ++pos_;
            out = std::move(node);
            return true;
        }
        if (s_[pos_] != '>') return false;
        ++pos_;

        for (;;) {
            if (!skipTextUntilTag()) return false;
            if (pos_ >= s_.size() || s_[pos_] != '<') return false;
            if (startsWith("</")) {
                pos_ += 2;
                std::string endTag;
                if (!parseName(endTag)) return false;
                skipWs();
                if (pos_ >= s_.size() || s_[pos_] != '>') return false;
                ++pos_;
                if (endTag != node->tag) return false;
                out = std::move(node);
                return true;
            }
            if (startsWith("<!--")) {
                if (!skipComment()) return false;
                continue;
            }
            if (startsWith("<?")) {
                if (!skipProcessingInstruction()) return false;
                continue;
            }
            if (startsWith("<![CDATA[")) {
                pos_ += 9;
                while (pos_ + 2 < s_.size()) {
                    if (s_[pos_] == ']' && s_[pos_ + 1] == ']' && s_[pos_ + 2] == '>') {
                        pos_ += 3;
                        break;
                    }
                    ++pos_;
                }
                continue;
            }
            std::unique_ptr<Node> child;
            if (!parseElement(child, node.get(), depth + 1)) return false;
            node->children.push_back(std::move(child));
        }
    }
};

}  // namespace

Document::Document()  = default;
Document::~Document() = default;
Document::Document(Document&&) noexcept            = default;
Document& Document::operator=(Document&&) noexcept = default;

Document Document::parse(const std::string& text, std::string* error) {
    Document doc;
    if (error) error->clear();
    Parser parser(text);
    if (!parser.parse(doc.root_, error)) {
        doc.root_.reset();
        return doc;
    }
    return doc;
}

std::string Element::tagName() const { return node_ ? node_->tag : std::string{}; }

Element Element::parent() const {
    return node_ && node_->parent ? Element(node_->parent) : Element();
}

bool Element::hasAttribute(const char* name) const {
    return node_ && name && node_->attributes.find(name) != node_->attributes.end();
}

std::string Element::getAttribute(const char* name, const std::string& fallback) const {
    if (!node_ || !name) return fallback;
    const auto it = node_->attributes.find(name);
    return it == node_->attributes.end() ? fallback : it->second;
}

int Element::getIntAttribute(const char* name, int fallback) const {
    if (!hasAttribute(name)) return fallback;
    try {
        return std::stoi(getAttribute(name));
    } catch (...) {
        return fallback;
    }
}

std::vector<Element> Element::children(const char* tag) const {
    std::vector<Element> out;
    if (!node_) return out;
    for (const auto& child : node_->children) {
        if (!tag || child->tag == tag) out.emplace_back(Element(child.get()));
    }
    return out;
}

std::vector<Element> Element::elementsByTag(const char* tag) const {
    std::vector<Element> out;
    if (!node_ || !tag) return out;
    std::function<void(const Node*)> walk = [&](const Node* node) {
        for (const auto& child : node->children) {
            if (child->tag == tag) out.emplace_back(Element(child.get()));
            walk(child.get());
        }
    };
    walk(node_);
    return out;
}

}  // namespace eve::xml
