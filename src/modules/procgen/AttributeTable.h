#pragma once

#include "common/Result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eve::procgen {

/** @brief Compact three-component value used by typed procedural metadata. */
struct ProcgenAttributeVector {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief Closed set of column types supported by procedural point metadata. */
enum class ProcgenAttributeType : std::uint8_t { Float, Int, Bool, Vector, String };

/**
 * @brief Schema-bearing column store aligned with a PointSet's point rows.
 *
 * Attribute names are interned once per table and each name owns exactly one typed column.
 * Missing values occupy an empty optional slot instead of allocating a map per point.
 * The table is an owning value, is not thread-safe for mutation, and invalidates borrowed
 * column storage after any schema or row-count mutation.
 */
class AttributeTable {
public:
    /** @brief Return the number of aligned point rows. */
    [[nodiscard]] std::size_t rowCount() const noexcept;
    /** @brief Resize every column to the same row count. */
    void resize(std::size_t rows);
    /** @brief Append one empty row and return its stable row index. */
    [[nodiscard]] std::size_t appendRow();
    /**
     * @brief Append one row copied from another table, merging compatible schema columns.
     * @param source Owning source table; may be this table.
     * @param sourceRow Row to copy.
     * @return New row index, or InvalidArgument/TypeMismatch without mutating this table.
     */
    [[nodiscard]] Result<std::size_t> appendRowFrom(const AttributeTable& source, std::size_t sourceRow);
    /** @brief Clear every value in one row while preserving the table schema. */
    [[nodiscard]] Result<void> clearRow(std::size_t row);
    /** @brief Remove all rows and schema columns. */
    void clear() noexcept;

    /** @brief Return the number of declared columns. */
    [[nodiscard]] std::size_t columnCount() const noexcept;
    /** @brief Return a stable insertion-ordered column name or an empty string. */
    [[nodiscard]] std::string_view columnName(std::size_t index) const noexcept;
    /** @brief Return the declared type for a name, or no value when absent. */
    [[nodiscard]] std::optional<ProcgenAttributeType> typeOf(std::string_view name) const;
    /** @brief Return whether a row contains a value in the named column. */
    [[nodiscard]] bool has(std::size_t row, std::string_view name) const;

    /** @brief Set a float value, rejecting an incompatible existing schema. */
    [[nodiscard]] Result<void> setFloat(std::size_t row, std::string_view name, float value);
    /** @brief Set an integer value, rejecting an incompatible existing schema. */
    [[nodiscard]] Result<void> setInt(std::size_t row, std::string_view name, std::int64_t value);
    /** @brief Set a Boolean value, rejecting an incompatible existing schema. */
    [[nodiscard]] Result<void> setBool(std::size_t row, std::string_view name, bool value);
    /** @brief Set a vector value, rejecting an incompatible existing schema. */
    [[nodiscard]] Result<void> setVector(std::size_t row, std::string_view name, ProcgenAttributeVector value);
    /** @brief Set a string value, rejecting an incompatible existing schema. */
    [[nodiscard]] Result<void> setString(std::size_t row, std::string_view name, std::string value);

    /** @brief Read a float value, or no value for an absent row, name, or type. */
    [[nodiscard]] std::optional<float> getFloat(std::size_t row, std::string_view name) const;
    /** @brief Read an integer value, or no value for an absent row, name, or type. */
    [[nodiscard]] std::optional<std::int64_t> getInt(std::size_t row, std::string_view name) const;
    /** @brief Read a Boolean value, or no value for an absent row, name, or type. */
    [[nodiscard]] std::optional<bool> getBool(std::size_t row, std::string_view name) const;
    /** @brief Read a vector value, or no value for an absent row, name, or type. */
    [[nodiscard]] std::optional<ProcgenAttributeVector> getVector(std::size_t row, std::string_view name) const;
    /** @brief Borrow a string value until the next table mutation, or return no value. */
    [[nodiscard]] std::optional<std::string_view> getString(std::size_t row, std::string_view name) const;

private:
    using FloatColumn  = std::vector<std::optional<float>>;
    using IntColumn    = std::vector<std::optional<std::int64_t>>;
    using BoolColumn   = std::vector<std::optional<bool>>;
    using VectorColumn = std::vector<std::optional<ProcgenAttributeVector>>;
    using StringColumn = std::vector<std::optional<std::string>>;
    using Storage      = std::variant<FloatColumn, IntColumn, BoolColumn, VectorColumn, StringColumn>;
    struct Column {
        ProcgenAttributeType type    = ProcgenAttributeType::Float;
        Storage              storage = FloatColumn{};
    };

    template <class T>
    [[nodiscard]] Result<void> set(std::size_t row, std::string_view name, ProcgenAttributeType type, T value);
    template <class T>
    [[nodiscard]] std::optional<T> get(std::size_t row, std::string_view name, ProcgenAttributeType type) const;
    [[nodiscard]] Result<Column*>  ensureColumn(std::string_view name, ProcgenAttributeType type);

    std::size_t                             rows_ = 0;
    std::unordered_map<std::string, Column> columns_;
    std::vector<std::string>                order_;
};

/** @brief Return the stable script/persistence name for an attribute type. */
[[nodiscard]] std::string_view procgenAttributeTypeName(ProcgenAttributeType type) noexcept;

}  // namespace eve::procgen
