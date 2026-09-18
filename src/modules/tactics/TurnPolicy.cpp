/**
 * @file TurnPolicy.cpp
 * @brief Built-in turn policies and the id-keyed policy registry.
 */

#include "tactics/TurnPolicy.h"

#include "common/Diagnostic.h"

#include <utility>

namespace eve::tactics {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

/**
 * @brief Group a side's units together, in declared side order.
 *
 * Units of the same side compare equivalent, so the caller's canonical-subject
 * tie-break decides their relative order. That keeps "sides alternate, and each
 * side's units keep a stable order" expressible without this policy also owning
 * the tie-break rule.
 */
class SideAlternatingPolicy final : public ITurnPolicy {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return kSideAlternatingPolicyId; }

    [[nodiscard]] TurnOrder order(const Battle&, const UnitOrder& left, const UnitOrder& right) const override {
        if (left.sideIndex < right.sideIndex) return TurnOrder::LeftFirst;
        if (right.sideIndex < left.sideIndex) return TurnOrder::RightFirst;
        return TurnOrder::Equivalent;
    }
};

/** @brief Order units by declared initiative, fastest first. */
class InitiativePolicy final : public ITurnPolicy {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return kInitiativePolicyId; }

    [[nodiscard]] TurnOrder order(const Battle&, const UnitOrder& left, const UnitOrder& right) const override {
        if (left.initiative > right.initiative) return TurnOrder::LeftFirst;
        if (right.initiative > left.initiative) return TurnOrder::RightFirst;
        return TurnOrder::Equivalent;
    }
};

}  // namespace

Result<void> TurnPolicyRegistry::add(std::shared_ptr<const ITurnPolicy> policy) {
    if (policy == nullptr || policy->id().empty())
        return failure<void>(DiagnosticCode::InvalidArgument, "tactics turn policy requires a non-empty stable id",
                             "turnPolicy.id");
    const std::string key(policy->id());
    if (policies_.contains(key))
        return failure<void>(DiagnosticCode::Conflict, "tactics turn policy id is already registered",
                             "turnPolicy.id");
    policies_.emplace(key, std::move(policy));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<const ITurnPolicy*> TurnPolicyRegistry::find(std::string_view id) const {
    const auto found = policies_.find(id);
    if (found == policies_.end())
        return failure<const ITurnPolicy*>(DiagnosticCode::NotFound, "tactics turn policy is not registered",
                                           "turnPolicy");
    return Result<const ITurnPolicy*>::success(found->second.get());
}

std::vector<std::string> TurnPolicyRegistry::ids() const {
    std::vector<std::string> result;
    result.reserve(policies_.size());
    for (const auto& [id, policy] : policies_) result.push_back(id);
    return result;
}

bool TurnPolicyRegistry::contains(std::string_view id) const { return policies_.contains(id); }

TurnPolicyRegistry& TurnPolicyRegistry::builtins() {
    // Function-local so registration happens once, after static initialisation
    // order is no longer a question.
    static TurnPolicyRegistry registry = [] {
        TurnPolicyRegistry value;
        // Both built-ins are immutable and stateless, so a failed registration is
        // impossible here; ignoring the result would hide a real duplicate bug.
        if (!value.add(std::make_shared<const SideAlternatingPolicy>()).ok()) return TurnPolicyRegistry{};
        if (!value.add(std::make_shared<const InitiativePolicy>()).ok()) return TurnPolicyRegistry{};
        return value;
    }();
    return registry;
}

}  // namespace eve::tactics
