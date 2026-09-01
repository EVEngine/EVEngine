#include "procgen/AttributeTable.h"

#include <utility>

namespace eve::procgen {
namespace {

template <class T>
std::vector<std::optional<T>> makeColumn(std::size_t rows) {
    return std::vector<std::optional<T>>(rows);
}

Result<void> invalidAttribute(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> attributeTypeMismatch(std::string_view name, ProcgenAttributeType expected, ProcgenAttributeType actual) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch,
                                                   "attribute '" + std::string(name) + "' has schema type " +
                                                       std::string(procgenAttributeTypeName(actual)) + ", not " +
                                                       std::string(procgenAttributeTypeName(expected)),
                                                   "name"));
}

}  // namespace

std::size_t AttributeTable::rowCount() const noexcept { return rows_; }
void        AttributeTable::resize(std::size_t rows) {
    for (auto& entry : columns_) std::visit([rows](auto& values) { values.resize(rows); }, entry.second.storage);
    rows_ = rows;
}
std::size_t AttributeTable::appendRow() {
    const std::size_t row = rows_;
    resize(rows_ + 1);
    return row;
}
Result<std::size_t> AttributeTable::appendRowFrom(const AttributeTable& source, std::size_t sourceRow) {
    if (sourceRow >= source.rows_)
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "source attribute row is out of range", "sourceRow"));

    for (const std::string& name : source.order_) {
        const auto sourceColumn = source.columns_.find(name);
        const auto targetColumn = columns_.find(name);
        if (targetColumn != columns_.end() && targetColumn->second.type != sourceColumn->second.type)
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::TypeMismatch,
                                  "attribute '" + name + "' has incompatible source and target schema types", "name"));
    }

    AttributeTable    staged = *this;
    const std::size_t row    = staged.appendRow();
    for (const std::string& name : source.order_) {
        const auto& sourceColumn = source.columns_.at(name);
        auto        ensured      = staged.ensureColumn(name, sourceColumn.type);
        if (!ensured.ok()) return Result<std::size_t>::failure(ensured.status());
        Column& targetColumn = *ensured.value();
        std::visit(
            [sourceRow, row, &sourceColumn](auto& targetValues) {
                using Values             = std::decay_t<decltype(targetValues)>;
                const auto& sourceValues = std::get<Values>(sourceColumn.storage);
                targetValues[row]        = sourceValues[sourceRow];
            },
            targetColumn.storage);
    }
    *this = std::move(staged);
    return Result<std::size_t>::success(row);
}
Result<void> AttributeTable::clearRow(std::size_t row) {
    if (row >= rows_) return invalidAttribute("attribute row is out of range", "row");
    for (auto& entry : columns_) std::visit([row](auto& values) { values[row].reset(); }, entry.second.storage);
    return Result<void>::success();
}
void AttributeTable::clear() noexcept {
    rows_ = 0;
    columns_.clear();
    order_.clear();
}
std::size_t      AttributeTable::columnCount() const noexcept { return order_.size(); }
std::string_view AttributeTable::columnName(std::size_t index) const noexcept {
    return index < order_.size() ? std::string_view(order_[index]) : std::string_view{};
}
std::optional<ProcgenAttributeType> AttributeTable::typeOf(std::string_view name) const {
    const auto found = columns_.find(std::string(name));
    return found == columns_.end() ? std::nullopt : std::optional<ProcgenAttributeType>{found->second.type};
}
bool AttributeTable::has(std::size_t row, std::string_view name) const {
    const auto found = columns_.find(std::string(name));
    if (found == columns_.end() || row >= rows_) return false;
    return std::visit([row](const auto& values) { return values[row].has_value(); }, found->second.storage);
}

Result<AttributeTable::Column*> AttributeTable::ensureColumn(std::string_view name, ProcgenAttributeType type) {
    const std::string key(name);
    auto              found = columns_.find(key);
    if (found != columns_.end()) {
        if (found->second.type != type) {
            auto mismatch = attributeTypeMismatch(name, type, found->second.type);
            return Result<Column*>::failure(mismatch.status());
        }
        return Result<Column*>::success(&found->second);
    }
    Column replacement;
    replacement.type = type;
    switch (type) {
        case ProcgenAttributeType::Float: replacement.storage = makeColumn<float>(rows_); break;
        case ProcgenAttributeType::Int: replacement.storage = makeColumn<std::int64_t>(rows_); break;
        case ProcgenAttributeType::Bool: replacement.storage = makeColumn<bool>(rows_); break;
        case ProcgenAttributeType::Vector: replacement.storage = makeColumn<ProcgenAttributeVector>(rows_); break;
        case ProcgenAttributeType::String: replacement.storage = makeColumn<std::string>(rows_); break;
    }
    auto inserted = columns_.emplace(key, std::move(replacement));
    try {
        order_.push_back(key);
    } catch (...) {
        columns_.erase(inserted.first);
        throw;
    }
    return Result<Column*>::success(&inserted.first->second);
}

template <class T>
Result<void> AttributeTable::set(std::size_t row, std::string_view name, ProcgenAttributeType type, T value) {
    if (name.empty()) return invalidAttribute("attribute name must not be empty", "name");
    if (row >= rows_) return invalidAttribute("attribute row is out of range", "row");
    auto column = ensureColumn(name, type);
    if (!column.ok()) return Result<void>::failure(column.status());
    std::get<std::vector<std::optional<T>>>(column.value()->storage)[row] = std::move(value);
    return Result<void>::success();
}

template <class T>
std::optional<T> AttributeTable::get(std::size_t row, std::string_view name, ProcgenAttributeType type) const {
    const auto found = columns_.find(std::string(name));
    if (found == columns_.end() || found->second.type != type || row >= rows_) return std::nullopt;
    return std::get<std::vector<std::optional<T>>>(found->second.storage)[row];
}

Result<void> AttributeTable::setFloat(std::size_t row, std::string_view name, float value) {
    return set(row, name, ProcgenAttributeType::Float, value);
}
Result<void> AttributeTable::setInt(std::size_t row, std::string_view name, std::int64_t value) {
    return set(row, name, ProcgenAttributeType::Int, value);
}
Result<void> AttributeTable::setBool(std::size_t row, std::string_view name, bool value) {
    return set(row, name, ProcgenAttributeType::Bool, value);
}
Result<void> AttributeTable::setVector(std::size_t row, std::string_view name, ProcgenAttributeVector value) {
    return set(row, name, ProcgenAttributeType::Vector, value);
}
Result<void> AttributeTable::setString(std::size_t row, std::string_view name, std::string value) {
    return set(row, name, ProcgenAttributeType::String, std::move(value));
}
std::optional<float> AttributeTable::getFloat(std::size_t row, std::string_view name) const {
    return get<float>(row, name, ProcgenAttributeType::Float);
}
std::optional<std::int64_t> AttributeTable::getInt(std::size_t row, std::string_view name) const {
    return get<std::int64_t>(row, name, ProcgenAttributeType::Int);
}
std::optional<bool> AttributeTable::getBool(std::size_t row, std::string_view name) const {
    return get<bool>(row, name, ProcgenAttributeType::Bool);
}
std::optional<ProcgenAttributeVector> AttributeTable::getVector(std::size_t row, std::string_view name) const {
    return get<ProcgenAttributeVector>(row, name, ProcgenAttributeType::Vector);
}
std::optional<std::string_view> AttributeTable::getString(std::size_t row, std::string_view name) const {
    const auto found = columns_.find(std::string(name));
    if (found == columns_.end() || found->second.type != ProcgenAttributeType::String || row >= rows_)
        return std::nullopt;
    const auto& value = std::get<StringColumn>(found->second.storage)[row];
    return value ? std::optional<std::string_view>(*value) : std::nullopt;
}

std::string_view procgenAttributeTypeName(ProcgenAttributeType type) noexcept {
    switch (type) {
        case ProcgenAttributeType::Float: return "float";
        case ProcgenAttributeType::Int: return "int";
        case ProcgenAttributeType::Bool: return "bool";
        case ProcgenAttributeType::Vector: return "vector";
        case ProcgenAttributeType::String: return "string";
    }
    return {};
}

}  // namespace eve::procgen
