#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace eve::editor {

/**
 * @brief Deterministic value tree shared by editor commands and automation.
 *
 * Objects use std::map deliberately so serialization and equality do not
 * depend on insertion order. Binary payloads and native pointers are excluded;
 * callers should pass stable resource or artifact identifiers instead.
 */
class EditorValue {
public:
    using Array   = std::vector<EditorValue>;
    using Object  = std::map<std::string, EditorValue>;
    using Storage = std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object>;

    enum class Type { Null, Bool, Integer, Number, String, Array, Object };

    EditorValue() = default;
    EditorValue(std::nullptr_t) {}
    EditorValue(bool value) : storage_(value) {}
    EditorValue(int value) : storage_(static_cast<int64_t>(value)) {}
    EditorValue(int64_t value) : storage_(value) {}
    EditorValue(float value) : storage_(static_cast<double>(value)) {}
    EditorValue(double value) : storage_(value) {}
    EditorValue(const char* value) : storage_(std::string(value ? value : "")) {}
    EditorValue(std::string value) : storage_(std::move(value)) {}
    EditorValue(Array value) : storage_(std::move(value)) {}
    EditorValue(Object value) : storage_(std::move(value)) {}

    /** @brief Return the active value type. */
    Type type() const;
    /** @brief Return the underlying variant for typed visitors. */
    const Storage& storage() const { return storage_; }
    Storage&       storage() { return storage_; }

    /** @brief Return a typed value pointer or nullptr on a type mismatch. */
    template <class T>
    const T* getIf() const {
        return std::get_if<T>(&storage_);
    }
    /** @brief Return a mutable typed value pointer or nullptr on mismatch. */
    template <class T>
    T* getIf() {
        return std::get_if<T>(&storage_);
    }

    /**
     * @brief Validate nesting, element count and approximate string byte limits.
     * @param maxDepth Maximum object/array nesting depth, including this value.
     * @param maxElements Maximum total array entries and object fields.
     * @param maxStringBytes Maximum total bytes across keys and string values.
     * @return True when all limits are satisfied.
     */
    bool withinLimits(size_t maxDepth, size_t maxElements, size_t maxStringBytes) const;

    bool operator==(const EditorValue&) const = default;

private:
    Storage storage_;
};

}  // namespace eve::editor
