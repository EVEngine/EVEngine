#include "editing/EditingValue.h"

#include <type_traits>

namespace eve::editing {

Value::Value() noexcept                                                               = default;
Value::~Value() noexcept                                                              = default;
Value::Value(const Value&)                                                            = default;
Value& Value::operator=(const Value&)                                                 = default;
Value::Value(Value&&) noexcept(std::is_nothrow_move_constructible_v<Storage>)         = default;
Value& Value::operator=(Value&&) noexcept(std::is_nothrow_move_assignable_v<Storage>) = default;
Value::Value(std::nullptr_t) {}
Value::Value(bool value) : storage_(value) {}
Value::Value(int value) : storage_(static_cast<int64_t>(value)) {}
Value::Value(int64_t value) : storage_(value) {}
Value::Value(float value) : storage_(static_cast<double>(value)) {}
Value::Value(double value) : storage_(value) {}
Value::Value(const char* value) : storage_(std::string(value ? value : "")) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(Array value) : storage_(std::move(value)) {}
Value::Value(Object value) : storage_(std::move(value)) {}
bool Value::operator==(const Value& other) const { return storage_ == other.storage_; }

Value::Type Value::type() const { return static_cast<Type>(storage_.index()); }

namespace {

struct ValueBudget {
    size_t elements    = 0;
    size_t stringBytes = 0;
};

bool checkValue(const Value& value, size_t depth, size_t maxDepth, size_t maxElements, size_t maxStringBytes,
                ValueBudget& budget) {
    if (depth > maxDepth) return false;
    return std::visit(
        [&](const auto& current) -> bool {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::string>) {
                budget.stringBytes += current.size();
                return budget.stringBytes <= maxStringBytes;
            } else if constexpr (std::is_same_v<T, Value::Array>) {
                budget.elements += current.size();
                if (budget.elements > maxElements) return false;
                for (const auto& entry : current)
                    if (!checkValue(entry, depth + 1, maxDepth, maxElements, maxStringBytes, budget)) return false;
                return true;
            } else if constexpr (std::is_same_v<T, Value::Object>) {
                budget.elements += current.size();
                if (budget.elements > maxElements) return false;
                for (const auto& [key, entry] : current) {
                    budget.stringBytes += key.size();
                    if (budget.stringBytes > maxStringBytes ||
                        !checkValue(entry, depth + 1, maxDepth, maxElements, maxStringBytes, budget))
                        return false;
                }
                return true;
            } else {
                return true;
            }
        },
        value.storage());
}

}  // namespace

bool Value::isWithinLimits(size_t maxDepth, size_t maxElements, size_t maxStringBytes) const {
    if (maxDepth == 0) return false;
    ValueBudget budget;
    return checkValue(*this, 1, maxDepth, maxElements, maxStringBytes, budget);
}

}  // namespace eve::editing

#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
template class __declspec(dllexport) std::vector<eve::editing::Value>;
template class __declspec(dllexport) std::map<std::string, eve::editing::Value>;
#endif

#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
template __declspec(dllexport) std::monostate* eve::editing::Value::getIf<std::monostate>();
template __declspec(dllexport) const std::monostate* eve::editing::Value::getIf<std::monostate>() const;
template __declspec(dllexport) bool*                 eve::editing::Value::getIf<bool>();
template __declspec(dllexport) const bool*           eve::editing::Value::getIf<bool>() const;
template __declspec(dllexport) int64_t*              eve::editing::Value::getIf<int64_t>();
template __declspec(dllexport) const int64_t*        eve::editing::Value::getIf<int64_t>() const;
template __declspec(dllexport) double*               eve::editing::Value::getIf<double>();
template __declspec(dllexport) const double*         eve::editing::Value::getIf<double>() const;
template __declspec(dllexport) std::string* eve::editing::Value::getIf<std::string>();
template __declspec(dllexport) const std::string* eve::editing::Value::getIf<std::string>() const;
template __declspec(dllexport) eve::editing::Value::Array* eve::editing::Value::getIf<eve::editing::Value::Array>();
template __declspec(dllexport) const                       eve::editing::Value::Array*
                               eve::editing::Value::getIf<eve::editing::Value::Array>() const;
template __declspec(dllexport) eve::editing::Value::Object* eve::editing::Value::getIf<eve::editing::Value::Object>();
template __declspec(dllexport) const                        eve::editing::Value::Object*
eve::editing::Value::getIf<eve::editing::Value::Object>() const;
#endif
