#include "attributes/AttributeResourceAccount.h"

#include <cstdint>
#include <string>
#include <utility>

namespace eve::attributes {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

}  // namespace

AttributeResourceAccountAdapter::AttributeResourceAccountAdapter(AttributeSetResourceAccount& account,
                                                                 AttributeResourceKind        kind) noexcept
    : account_(account), kind_(kind) {}

const char* AttributeResourceAccountAdapter::resourceName(AttributeResourceKind kind) noexcept {
    switch (kind) {
        case AttributeResourceKind::Mana: return "mana";
        case AttributeResourceKind::Stamina: return "stamina";
    }
    return nullptr;
}

eve::Result<eve::resource::CostSpec> AttributeResourceAccountAdapter::makeCost(AttributeResourceKind kind,
                                                                               std::int64_t          amount) {
    const char* name = resourceName(kind);
    if (name == nullptr) return invalid<eve::resource::CostSpec>("attribute resource kind is invalid", "resource.kind");
    return eve::resource::CostSpec::single(name, amount);
}

eve::Result<void> AttributeResourceAccountAdapter::validateCost(const eve::resource::CostSpec& cost) const {
    const char* name = resourceName(kind_);
    if (name == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "attribute resource kind is invalid", "resource.kind"));
    if (!cost.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "attribute resource view requires a validated CostSpec", "cost"));
    if (cost.size() != 1 || cost.items().front().resource.value() != name)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "named attribute resource view received a different resource", "cost.resource"));
    return eve::Result<void>::success();
}

eve::Result<eve::resource::Affordability> AttributeResourceAccountAdapter::canAfford(
    const eve::resource::CostSpec& cost) const {
    auto valid = validateCost(cost);
    if (!valid) return eve::Result<eve::resource::Affordability>::failure(valid.status());
    return account_.canAfford(cost);
}

eve::Result<eve::resource::Reservation> AttributeResourceAccountAdapter::reserve(const eve::resource::CostSpec& cost) {
    auto valid = validateCost(cost);
    if (!valid) return eve::Result<eve::resource::Reservation>::failure(valid.status());
    return account_.reserve(cost);
}

eve::Result<eve::resource::Receipt> AttributeResourceAccountAdapter::debit(const eve::resource::CostSpec& cost) {
    auto valid = validateCost(cost);
    if (!valid) return eve::Result<eve::resource::Receipt>::failure(valid.status());
    return account_.debit(cost);
}

eve::Result<eve::resource::Receipt> AttributeResourceAccountAdapter::credit(const eve::resource::CostSpec& cost) {
    auto valid = validateCost(cost);
    if (!valid) return eve::Result<eve::resource::Receipt>::failure(valid.status());
    return account_.credit(cost);
}

eve::Result<eve::resource::Receipt> AttributeResourceAccountAdapter::commit(
    const eve::resource::Reservation& reservation) {
    const char* name = resourceName(kind_);
    if (name == nullptr || !reservation.isValid() || reservation.cost.size() != 1 ||
        reservation.cost.items().front().resource.value() != name)
        return eve::Result<eve::resource::Receipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "named attribute resource view received a foreign reservation cost",
            "reservation.cost"));
    return account_.commit(reservation);
}

eve::Result<void> AttributeResourceAccountAdapter::rollback(const eve::resource::Reservation& reservation) {
    const char* name = resourceName(kind_);
    if (name == nullptr || !reservation.isValid() || reservation.cost.size() != 1 ||
        reservation.cost.items().front().resource.value() != name)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "named attribute resource view received a foreign reservation cost",
            "reservation.cost"));
    return account_.rollback(reservation);
}

}  // namespace eve::attributes
