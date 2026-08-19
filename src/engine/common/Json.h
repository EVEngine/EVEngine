#pragma once

// Read-only JSON facade shared by every module that loads data-driven config.
//
// The backend is a self-contained recursive-descent parser (Json.cpp) rather
// than Poco, so EVCommon stays dependency-free and modules that only read JSON
// (rpg / inventory / building / card / voxel / housegen / i18n) no longer pull
// Poco in just to parse a config file. Writing JSON still goes through
// data::JsonDocument / Poco.
//
// Accessors never throw: a missing key, a wrong type or an out-of-range index
// yields the supplied fallback (or a null Value), which is what config loading
// wants and what the per-module helpers this replaces already did.

#include "common/Export.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::json {

struct Node;
class Document;

/**
 * Handle to one node of a parsed Document. Cheap to copy, but only valid while
 * the owning Document is alive.
 */
class EVENGINE_API Value {
public:
    Value() = default;

    /** True when this handle refers to an actual node. */
    explicit operator bool() const { return node_ != nullptr; }

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isObject() const;
    bool isArray() const;

    // --- object access -----------------------------------------------------
    bool  has(const char* key) const;
    /** Member value, or a null Value when absent / not an object. */
    Value get(const char* key) const;
    /** Member names in document order; empty when not an object. */
    std::vector<std::string> keys() const;

    // --- array access ------------------------------------------------------
    /** Element count for arrays, member count for objects, else 0. */
    size_t size() const;
    /** Element at index, or a null Value when out of range / not an array. */
    Value at(size_t index) const;

    // --- scalars -----------------------------------------------------------
    // Conversions are lenient (a numeric string reads as a number and back)
    // and fall back rather than throw.
    bool        asBool(bool fallback = false) const;
    int         asInt(int fallback = 0) const;
    float       asFloat(float fallback = 0.f) const;
    double      asDouble(double fallback = 0.0) const;
    std::string asString(const std::string& fallback = {}) const;

    // --- keyed shorthand ---------------------------------------------------
    // get(key).asX(fallback), which is the dominant shape in config loaders.
    bool        getBool(const char* key, bool fallback = false) const;
    int         getInt(const char* key, int fallback = 0) const;
    float       getFloat(const char* key, float fallback = 0.f) const;
    double      getDouble(const char* key, double fallback = 0.0) const;
    std::string getString(const char* key, const std::string& fallback = {}) const;

    // --- collections -------------------------------------------------------
    // Empty when the key is absent or the value has the wrong shape.
    std::vector<std::string> getStringArray(const char* key) const;
    std::vector<int>         getIntArray(const char* key) const;
    std::vector<float>       getFloatArray(const char* key) const;
    std::unordered_map<std::string, std::string> getStringMap(const char* key) const;
    std::unordered_map<std::string, int>         getIntMap(const char* key) const;

    /** This node read as an array of strings (for a Value already in hand). */
    std::vector<std::string> toStringArray() const;

private:
    friend class Document;
    explicit Value(const Node* node) : node_(node) {}
    const Node* node_ = nullptr;
};

/**
 * Owns a parsed JSON tree. Move-only; every Value handed out points into it.
 */
class EVENGINE_API Document {
public:
    Document();
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    /**
     * Parse `text`. On failure the Document is invalid, `error` (when given)
     * describes the problem, and root() returns a null Value.
     */
    static Document parse(const std::string& text, std::string* error = nullptr);

    bool  valid() const { return root_ != nullptr; }
    Value root() const { return Value(root_.get()); }

private:
    std::unique_ptr<Node> root_;
};

}  // namespace eve::json
