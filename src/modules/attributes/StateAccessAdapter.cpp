#include "attributes/StateAccessAdapter.h"

#include <cmath>
#include <string>
#include <utility>

namespace eve::attributes {
namespace {

eve::Result<eve::MutationReceipt> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<eve::MutationReceipt>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

bool numeric(const eve::Value& value, double& out) {
    if (value.isInt64()) {
        out = static_cast<double>(value.asInt());
        return true;
    }
    if (value.isDouble()) {
        out = value.asDouble();
        return std::isfinite(out);
    }
    return false;
}

}  // namespace

bool AttributeSetStateAdapter::owns(std::string_view subject) const noexcept {
    return !subject.empty() && !attributes_.subject().empty() && attributes_.subject() == subject;
}

std::optional<eve::Value> AttributeSetStateAdapter::value(std::string_view subject, std::string_view key) const {
    if (!owns(subject) || key.empty() || !attributes_.has(std::string(key))) return std::nullopt;
    return eve::Value(attributes_.getFinal(std::string(key)));
}

std::optional<eve::Value> AttributeSetStateAdapter::attribute(std::string_view subject, std::string_view key) const {
    return value(subject, key);
}

std::optional<eve::Value> AttributeSetStateAdapter::state(std::string_view subject, std::string_view key) const {
    return value(subject, key);
}

eve::Result<eve::MutationReceipt> AttributeSetStateAdapter::apply(std::span<const eve::StateMutation> mutations,
                                                                  const eve::MutationContext&         context) {
    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const auto& mutation = mutations[index];
        if (mutation.persistent)
            return failure(eve::DiagnosticCode::Unsupported,
                           "AttributeSet mutations are volatile; use StatePatch for persistence",
                           "mutations[" + std::to_string(index) + "]");
        if (!owns(mutation.subject) || mutation.key.empty())
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "attribute mutation requires this set's subject and a key",
                           "mutations[" + std::to_string(index) + "]");
        if (mutation.kind != eve::MutationKind::Set && mutation.kind != eve::MutationKind::AddNumber)
            return failure(eve::DiagnosticCode::Unsupported, "AttributeSet supports Set and AddNumber only",
                           "mutations[" + std::to_string(index) + "]");
        double ignored = 0.0;
        if (!numeric(mutation.value, ignored))
            return failure(eve::DiagnosticCode::InvalidArgument, "attribute mutation value must be a finite number",
                           "mutations[" + std::to_string(index) + "].value");
    }

    AttributeSet candidate = attributes_;
    std::size_t  changed   = 0;
    for (const auto& mutation : mutations) {
        const double amount =
            mutation.value.isInt64() ? static_cast<double>(mutation.value.asInt()) : mutation.value.asDouble();
        const double before = candidate.getFinal(mutation.key);
        if (mutation.kind == eve::MutationKind::Set)
            candidate.setBase(mutation.key, amount);
        else
            candidate.modifyBase(mutation.key, amount);
        const double after = candidate.getFinal(mutation.key);
        if (before != after || !attributes_.has(mutation.key)) ++changed;
    }
    attributes_ = std::move(candidate);
    return eve::Result<eve::MutationReceipt>::success(
        eve::MutationReceipt{context.transactionId, changed},
        eve::Status::success(changed == 0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
}

}  // namespace eve::attributes
