#pragma once

#include "common/Export.h"

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace eve::presentation {

/**
 * @brief Pointer-free deterministic value shared by UI, editor and automation.
 *
 * Object keys use std::map so equality and serialized traversal are stable.
 * Native handles intentionally stay outside this type; models expose stable IDs.
 */
class EVENGINE_API Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using Storage =
        std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;

    enum class Type { Null, Bool, Integer, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : storage_(value) {}
    Value(int value) : storage_(static_cast<std::int64_t>(value)) {}
    Value(std::int64_t value) : storage_(value) {}
    Value(float value) : storage_(static_cast<double>(value)) {}
    Value(double value) : storage_(value) {}
    Value(const char *value) : storage_(std::string(value ? value : "")) {}
    Value(std::string value) : storage_(std::move(value)) {}
    Value(Array value) : storage_(std::move(value)) {}
    Value(Object value) : storage_(std::move(value)) {}

    /** @brief Return the active value type. */
    Type type() const { return static_cast<Type>(storage_.index()); }
    /** @brief Return the underlying variant for typed visitors. */
    const Storage &storage() const { return storage_; }
    /** @brief Return the mutable underlying variant. */
    Storage &storage() { return storage_; }

    /** @brief Return a typed pointer, or nullptr when the type differs. */
    template <class T> const T *getIf() const { return std::get_if<T>(&storage_); }
    /** @brief Return a mutable typed pointer, or nullptr when the type differs. */
    template <class T> T *getIf() { return std::get_if<T>(&storage_); }

    bool operator==(const Value &) const = default;

private:
    Storage storage_;
};

}  // namespace eve::presentation
