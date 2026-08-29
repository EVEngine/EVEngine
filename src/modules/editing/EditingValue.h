#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace eve::editing {

/**
 * @brief Deterministic owning value tree shared by authoring hosts.
 *
 * Objects use std::map so serialization and equality do not depend on insertion
 * order. Native pointers and runtime handles are intentionally excluded.
 */
class Value {
public:
    using Array   = std::vector<Value>;
    using Object  = std::map<std::string, Value>;
    using Storage = std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object>;

    enum class Type { Null, Bool, Integer, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : storage_(value) {}
    Value(int value) : storage_(static_cast<int64_t>(value)) {}
    Value(int64_t value) : storage_(value) {}
    Value(float value) : storage_(static_cast<double>(value)) {}
    Value(double value) : storage_(value) {}
    Value(const char* value) : storage_(std::string(value ? value : "")) {}
    Value(std::string value) : storage_(std::move(value)) {}
    Value(Array value) : storage_(std::move(value)) {}
    Value(Object value) : storage_(std::move(value)) {}

    /** @brief Return the active value type. */
    Type type() const;
    /** @brief Return the underlying variant for typed visitors. */
    const Storage& storage() const { return storage_; }
    /** @brief Return mutable storage; all child borrows are invalidated by mutation. */
    Storage& storage() { return storage_; }

    /**
     * @brief Return an immediately borrowed typed value.
     * @return Borrowed pointer into this value, or null on type mismatch.
     * @lifetime Valid until this Value is mutated or destroyed.
     */
    template <class T>
    const T* getIf() const {
        return std::get_if<T>(&storage_);
    }
    /**
     * @brief Return an immediately borrowed mutable value.
     * @return Borrowed pointer into this value, or null on type mismatch.
     * @lifetime Valid until this Value is mutated or destroyed.
     */
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
    bool isWithinLimits(size_t maxDepth, size_t maxElements, size_t maxStringBytes) const;

    bool operator==(const Value&) const = default;

private:
    Storage storage_;
};

}  // namespace eve::editing
