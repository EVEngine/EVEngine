#pragma once
#include "common/Export.h"


#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace eve::editing {

/**
 * @brief Deterministic owning value tree shared by authoring hosts.
 *
 * @note Owns all children; copies are independent and moves transfer ownership.
 * @note Mutations invalidate child borrows; callers synchronize access. No callbacks are invoked.
 *
 * Objects use std::map so serialization and equality do not depend on insertion
 * order. Native pointers and runtime handles are intentionally excluded.
 */
class EVENGINE_API_PLATFORM Value {
public:
    using Array   = std::vector<Value>;
    using Object  = std::map<std::string, Value>;
    using Storage = std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object>;

    /** @brief Type public API. */
    enum class Type { Null, Bool, Integer, Number, String, Array, Object };

    /** @brief Construct a null owning value. */
    Value() noexcept;
    /** @brief Release the owned value tree. */
    ~Value() noexcept;
    /** @brief Deep-copy the complete value tree. */
    Value(const Value& other);
    /** @brief Replace this value with a deep copy. */
    Value& operator=(const Value& other);
    /** @brief Transfer ownership of the complete value tree. */
    Value(Value&& other) noexcept(std::is_nothrow_move_constructible_v<Storage>);
    /** @brief Replace this value by transferring tree ownership. */
    Value& operator=(Value&& other) noexcept(std::is_nothrow_move_assignable_v<Storage>);
    /** @brief Construct a null value. */
    Value(std::nullptr_t);
    /** @brief Construct a boolean value. */
    Value(bool value);
    /** @brief Construct an integer value. */
    Value(int value);
    /** @brief Construct a signed 64-bit integer value. */
    Value(int64_t value);
    /** @brief Construct a number from a float. */
    Value(float value);
    /** @brief Construct a number from a double. */
    Value(double value);
    /** @brief Copy a NUL-terminated string; a null input becomes empty text. */
    Value(const char* value);
    /** @brief Transfer ownership of string storage. */
    Value(std::string value);
    /** @brief Transfer ownership of array entries. */
    Value(Array value);
    /** @brief Transfer ownership of ordered object fields. */
    Value(Object value);

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

    /** @brief Compare complete trees by value, independent of object insertion order. */
    bool operator==(const Value&) const;

private:
    Storage storage_;
};

}  // namespace eve::editing

// Platform owns the Windows DLL container operations. Elements retain their
// independent storage; consumers use the same CRT and iterator configuration.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_PLATFORM)
extern template class __declspec(dllimport) std::vector<eve::editing::Value>;
extern template class __declspec(dllimport) std::map<std::string, eve::editing::Value>;
#endif

// Share common Windows DLL getters without changing the variant or its borrows.
// Returned pointers still refer directly into the caller-owned Value.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_PLATFORM)
extern template __declspec(dllimport) std::monostate* eve::editing::Value::getIf<std::monostate>();
extern template __declspec(dllimport) const std::monostate* eve::editing::Value::getIf<std::monostate>() const;
extern template __declspec(dllimport) bool*                 eve::editing::Value::getIf<bool>();
extern template __declspec(dllimport) const bool*           eve::editing::Value::getIf<bool>() const;
extern template __declspec(dllimport) int64_t*              eve::editing::Value::getIf<int64_t>();
extern template __declspec(dllimport) const int64_t*        eve::editing::Value::getIf<int64_t>() const;
extern template __declspec(dllimport) double*               eve::editing::Value::getIf<double>();
extern template __declspec(dllimport) const double*         eve::editing::Value::getIf<double>() const;
extern template __declspec(dllimport) std::string* eve::editing::Value::getIf<std::string>();
extern template __declspec(dllimport) const std::string* eve::editing::Value::getIf<std::string>() const;
extern template __declspec(dllimport)                    eve::editing::Value::Array*
                                                         eve::editing::Value::getIf<eve::editing::Value::Array>();
extern template __declspec(dllimport) const              eve::editing::Value::Array*
                                                         eve::editing::Value::getIf<eve::editing::Value::Array>() const;
extern template __declspec(dllimport)                    eve::editing::Value::Object*
                                                         eve::editing::Value::getIf<eve::editing::Value::Object>();
extern template __declspec(dllimport) const              eve::editing::Value::Object*
eve::editing::Value::getIf<eve::editing::Value::Object>() const;
#endif
