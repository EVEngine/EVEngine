#pragma once

/**
 * @file Value.h
 * @brief Owning, renderer-independent dynamic values.
 */

#include "common/Export.h"
#include "common/Result.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace eve {

/**
 * @brief The canonical owning dynamic value used by data-facing protocols.
 *
 * A Value owns all recursively contained storage. Object members use
 * `std::map`, so key traversal and compact JSON serialization are
 * deterministic. Native pointers, ECS handles, renderer objects and other
 * domain-specific values are intentionally not part of this protocol.
 */
class EVENGINE_API_FOUNDATION Value {
public:
    using Array   = std::vector<Value>;
    using Object  = std::map<std::string, Value>;
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;

    /** @brief Runtime kind of the owned value. */
    enum class Type : std::uint8_t {
        Null = 0,
        Bool,
        Int64,
        Double,
        String,
        Array,
        Object,
        /** @brief Compatibility spelling for Int64. */
        Integer = Int64,
        /** @brief Compatibility spelling for Double. */
        Number = Double,
        /** @brief Compatibility spelling used by the former dialogue value. */
        Int = Int64,
        /** @brief Compatibility spelling used by the former dialogue value. */
        Float = Double,
    };

    /** @brief Compatibility spelling for code that used StateValue::Kind. */
    using Kind = Type;

    /** @brief Construct a null value. */
    Value() noexcept;
    /** @brief Construct a null value from nullptr. */
    Value(std::nullptr_t);
    /** @brief Construct a boolean value. */
    Value(bool value);
    /** @brief Construct an Int64 value without narrowing. */
    Value(std::int64_t value);
    /** @brief Construct an integer value using the canonical Int64 type. */
    Value(int value);
    /** @brief Construct a Double value; non-finite values serialize as errors. */
    Value(double value);
    /** @brief Construct a Double value from a float. */
    Value(float value);
    /** @brief Construct a string value, copying the supplied text. */
    Value(const char* value);
    /** @brief Construct a string value by taking the supplied text. */
    Value(std::string value);
    /** @brief Construct an array value by taking its elements. */
    Value(Array value);
    /** @brief Construct an object value by taking its members. */
    Value(Object value);

    /** @brief Destroy the recursively owned payload. */
    ~Value() noexcept;
    /** @brief Copy the recursively owned payload. */
    Value(const Value& other);
    /** @brief Move the owned payload without copying. */
    Value(Value&& other) noexcept;
    /** @brief Replace this payload with a recursive copy. */
    Value& operator=(const Value& other);
    /** @brief Replace this payload by moving ownership. */
    Value& operator=(Value&& other) noexcept;

    /** @brief Return the active value kind. */
    Type type() const noexcept { return static_cast<Type>(storage_.index()); }
    /** @brief Compatibility spelling for the former dialogue data tree. */
    Type kind() const noexcept { return type(); }
    /** @brief Return true when this value is null. */
    bool isNull() const noexcept { return type() == Type::Null; }
    /** @brief Return true when this value is a boolean. */
    bool isBool() const noexcept { return type() == Type::Bool; }
    /** @brief Return true when this value is an Int64. */
    bool isInt64() const noexcept { return type() == Type::Int64; }
    /** @brief Return true when this value is a Double. */
    bool isDouble() const noexcept { return type() == Type::Double; }
    /** @brief Return true when this value is a string. */
    bool isString() const noexcept { return type() == Type::String; }
    /** @brief Return true when this value is an array. */
    bool isArray() const noexcept { return type() == Type::Array; }
    /** @brief Return true when this value is an object. */
    bool isObject() const noexcept { return type() == Type::Object; }

    /** @brief Compatibility factory for a null value. */
    static Value null() { return Value(); }
    /** @brief Compatibility factory for an Int64 value. */
    static Value integer(std::int64_t value) { return Value(value); }
    /** @brief Compatibility factory for a Double value. */
    static Value number(double value) { return Value(value); }
    /** @brief Compatibility factory for a boolean value. */
    static Value boolean(bool value) { return Value(value); }
    /** @brief Compatibility factory for a string value. */
    static Value string(std::string value) { return Value(std::move(value)); }
    /** @brief Compatibility factory for an array value. */
    static Value array(Array value);
    /** @brief Compatibility factory for an object value. */
    static Value object(Object value);
    /**
     * @brief Copy elements into an owning array without a caller-side vector temporary.
     * @param values Borrowed elements, copied before return; no references are retained.
     * @return Independent array value.
     * @cost Linear in the number and recursively owned size of elements, once per construction.
     * @thread No shared mutable state; inputs must remain stable during the call.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] static Value array(std::initializer_list<Value> values);
    /**
     * @brief Copy members into an owning object with lexicographically ordered keys.
     * @param members Borrowed members, copied before return; duplicate keys keep the first entry.
     * @return Independent object value; no references to members are retained.
     * @cost Map insertion plus recursive value copies, once per construction.
     * @thread No shared mutable state; inputs must remain stable during the call.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] static Value object(std::initializer_list<Object::value_type> members);

    /** @brief Return the Int64 payload; the caller must have checked the kind. */
    std::int64_t asInt() const;
    /** @brief Return the Double payload; the caller must have checked the kind. */
    double asDouble() const;
    /** @brief Return the boolean payload; the caller must have checked the kind. */
    bool asBool() const;
    /** @brief Return the string payload; the caller must have checked the kind. */
    const std::string& asString() const;

    /** @brief Append an element to an array value. */
    void pushBack(Value value);
    /** @brief Return the number of array elements. */
    std::size_t arraySize() const;
    /** @brief Return a bounds-checked array element. */
    Value& at(std::size_t index);
    /** @brief Return a bounds-checked immutable array element. */
    const Value& at(std::size_t index) const;
    /** @brief Insert or replace an object member. */
    void set(const std::string& key, Value value);
    /**
     * @brief Return an object member, or nullptr when absent.
     * @return Borrowed mutable pointer into this Value; nullptr when not an object or key is absent.
     * @ownership Borrowed; this Value owns the returned member.
     * @nullable Yes.
     * @lifetime Valid until this Value is destroyed, moved, or structurally mutated.
     * @thread Affine to this Value; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    Value* find(const std::string& key) noexcept;
    /**
     * @brief Return an immutable object member, or nullptr when absent.
     * @return Borrowed const pointer into this Value; nullptr when not an object or key is absent.
     * @ownership Borrowed; this Value owns the returned member.
     * @nullable Yes.
     * @lifetime Valid until this Value is destroyed, moved, or structurally mutated.
     * @thread Affine to this Value; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    const Value* find(const std::string& key) const noexcept;
    /** @brief Return object member names in deterministic order. */
    std::vector<std::string> keys() const;

    /** @brief Return whether this value is an integer or finite number. */
    bool isNumeric() const noexcept { return isInt64() || isDouble(); }
    /** @brief Return the compatibility type name used by dialogue scripts. */
    std::string typeName() const;
    /** @brief Render a scalar using the former dialogue formatting rules. */
    std::string toString() const;

    /** @brief Return the immutable variant storage for visitors. */
    const Storage& storage() const noexcept { return storage_; }
    /** @brief Return mutable variant storage for owning edits. */
    Storage& storage() noexcept { return storage_; }

    /**
     * @brief Return a typed pointer, or nullptr when the kind differs.
     * @return Borrowed const pointer into this Value's variant storage.
     * @ownership Borrowed; this Value owns the storage.
     * @nullable Yes when `T` is not the active type.
     * @lifetime Valid until this Value is destroyed, moved, or assigned a different kind.
     * @thread Affine to this Value; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    template <class T>
    const T* getIf() const noexcept {
        return std::get_if<T>(&storage_);
    }

    /**
     * @brief Return a mutable typed pointer, or nullptr when the kind differs.
     * @return Borrowed mutable pointer into this Value's variant storage.
     * @ownership Borrowed; this Value owns the storage.
     * @nullable Yes when `T` is not the active type.
     * @lifetime Valid until this Value is destroyed, moved, or assigned a different kind.
     * @thread Affine to this Value; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    template <class T>
    T* getIf() noexcept {
        return std::get_if<T>(&storage_);
    }

    /**
     * @brief Parse one strict JSON value into an owning Value.
     * @param json UTF-8 JSON text; duplicate object keys are rejected.
     * @return The parsed value, or a ParseError diagnostic.
     * @remarks JSON numbers without a fraction or exponent must fit Int64;
     *          fractional and exponent forms become finite Double values.
     */
    [[nodiscard]] static Result<Value> fromJson(std::string_view json);

    /**
     * @brief Serialize this value as deterministic compact JSON.
     * @return JSON text, or a SerializationError for NaN or infinity.
     * @remarks Object keys are emitted in lexicographic order. Non-finite
     *          Double values are intentionally rejected rather than silently
     *          converted to null.
     */
    [[nodiscard]] Result<std::string> toJson() const;

    /** @brief Compare two values recursively, including their numeric kinds. */
    bool operator==(const Value&) const;

private:
    Storage storage_;
};

}  // namespace eve

// One compiled owner for the canonical containers. Member templates remain
// available in consumers; their non-template operations share foundation code.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_FOUNDATION)
extern template class __declspec(dllimport) std::vector<eve::Value>;
extern template class __declspec(dllimport) std::map<std::string, eve::Value>;
#else
extern template class std::vector<eve::Value>;
extern template class std::map<std::string, eve::Value>;
#endif
