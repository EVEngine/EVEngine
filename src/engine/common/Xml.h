#pragma once

// Lightweight read-only XML facade for config / tileset documents (e.g. Tiled
// TSX). Self-contained recursive-descent parser — no Poco — so EVCommon stays
// dependency-free and modules that only need attribute-oriented XML do not pull
// the full Poco DOM stack. Writing XML is out of scope; use data::XmlDocument
// (Poco) only for legacy mutable encode/decode round-trips.
//
// Accessors never throw: a missing attribute or tag yields the supplied
// fallback (or an empty Element).

#include "common/Export.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::xml {

struct Node;
class Document;

/**
 * Handle to one element of a parsed Document. Cheap to copy; valid while the
 * owning Document lives.
 */
class EVENGINE_API_FOUNDATION Element {
public:
    Element() = default;

    /** True when this handle refers to an actual element. */
    explicit operator bool() const { return node_ != nullptr; }

    bool operator==(const Element& other) const { return node_ == other.node_; }
    bool operator!=(const Element& other) const { return node_ != other.node_; }

    /** Local tag name (no namespace prefix stripping). Empty when invalid. */
    std::string tagName() const;

    /** Parent element, or a null Element for the document root / invalid. */
    Element parent() const;

    bool        hasAttribute(const char* name) const;
    std::string getAttribute(const char* name, const std::string& fallback = {}) const;
    int         getIntAttribute(const char* name, int fallback = 0) const;

    /**
     * Direct child elements. When `tag` is non-null, only children whose tag
     * equals `tag` are returned.
     */
    std::vector<Element> children(const char* tag = nullptr) const;

    /**
     * Descendant elements with the given tag (document order), matching the
     * common getElementsByTagName shape used by config loaders.
     */
    std::vector<Element> elementsByTag(const char* tag) const;

private:
    friend class Document;
    explicit Element(const Node* node) : node_(node) {}
    const Node* node_ = nullptr;
};

/**
 * Owns a parsed XML element tree. Move-only; every Element points into it.
 */
class EVENGINE_API_FOUNDATION Document {
public:
    Document();
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&)            = delete;
    Document& operator=(const Document&) = delete;

    /**
     * Parse `text`. On failure the Document is invalid, `error` (when given)
     * describes the problem, and root() returns a null Element.
     */
    static Document parse(const std::string& text, std::string* error = nullptr);

    bool    valid() const { return root_ != nullptr; }
    Element root() const { return Element(root_.get()); }

private:
    std::unique_ptr<Node> root_;
};

}  // namespace eve::xml
