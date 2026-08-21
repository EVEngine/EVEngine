#pragma once

#include "common/Export.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve {

/**
 * @brief JSON-compatible state value tree used by state hot reload.
 *
 * Engine modules (state providers) and script roots serialize their runtime
 * state into this structure so a reload session can capture it before code
 * changes and restore it afterwards. Scalars, arrays and objects map 1:1 to
 * JSON; object members preserve insertion order.
 */
class EVENGINE_API StateValue {
public:
    enum class Kind { Null, Int, Float, Bool, String, Array, Object };

    /** @brief Null value. */
    StateValue() = default;
    /** @brief Null value. */
    static StateValue null() { return StateValue(); }
    /** @brief Integer value. */
    static StateValue integer(int64_t v);
    /** @brief Floating-point value. */
    static StateValue number(double v);
    /** @brief Boolean value. */
    static StateValue boolean(bool v);
    /** @brief String value. */
    static StateValue string(std::string v);
    /** @brief Empty array. */
    static StateValue array() { return StateValue(Kind::Array); }
    /** @brief Empty object. */
    static StateValue object() { return StateValue(Kind::Object); }

    /** @brief The kind of the stored value. */
    Kind kind() const { return kind_; }

    bool isNull() const { return kind_ == Kind::Null; }
    bool isInt() const { return kind_ == Kind::Int; }
    bool isFloat() const { return kind_ == Kind::Float; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isString() const { return kind_ == Kind::String; }
    bool isObject() const { return kind_ == Kind::Object; }
    bool isArray() const { return kind_ == Kind::Array; }

    /** @brief Integer payload; only valid when kind() == Kind::Int. */
    int64_t asInt() const { return i_; }
    /** @brief Floating-point payload; only valid when kind() == Kind::Float. */
    double asDouble() const { return f_; }
    /** @brief Boolean payload; only valid when kind() == Kind::Bool. */
    bool asBool() const { return b_; }
    /** @brief String payload; only valid when kind() == Kind::String. */
    const std::string& asString() const { return s_; }

    /** @brief Append an element; only valid on arrays. */
    void pushBack(StateValue v);
    /** @brief Element count; only valid on arrays. */
    size_t arraySize() const { return arr_.size(); }
    /** @brief Indexed element (bounds-checked); only valid on arrays. */
    StateValue&       at(size_t index) { return arr_.at(index); }
    const StateValue& at(size_t index) const { return arr_.at(index); }

    /** @brief Insert or replace `key`; only valid on objects. */
    void set(const std::string& key, StateValue v);
    /** @brief Look up `key`; nullptr when absent. */
    const StateValue* find(const std::string& key) const;
    StateValue*       find(const std::string& key);
    /** @brief Member names in insertion order. */
    std::vector<std::string> keys() const;

    /**
     * @brief Navigate a dotted path ("a.b[2].c").
     * @return nullptr when any segment is missing or the wrong type.
     */
    const StateValue* get(const std::string& dottedPath) const;
    StateValue*       get(const std::string& dottedPath);

    /**
     * @brief Set a value at a dotted path, creating missing intermediate
     *        objects along the way.
     * @return false when an existing segment is the wrong type or an array
     *         index is out of bounds (arrays must be pre-sized via pushBack).
     */
    bool setPath(const std::string& dottedPath, StateValue v);

    /**
     * @brief Fill missing members from `defaults`; object members merge
     *        recursively, existing values are never overwritten.
     * @return true when at least one member was added.
     */
    bool mergeDefaults(const StateValue& defaults);

    bool operator==(const StateValue& o) const;
    bool operator!=(const StateValue& o) const { return !(*this == o); }

private:
    explicit StateValue(Kind kind) : kind_(kind) {}

    Kind                                            kind_ = Kind::Null;
    int64_t                                         i_    = 0;
    double                                          f_    = 0.0;
    bool                                            b_    = false;
    std::string                                     s_;
    std::vector<StateValue>                         arr_;
    std::vector<std::pair<std::string, StateValue>> obj_;
};

}  // namespace eve
